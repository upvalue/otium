(library (otium conditions)
  (export error signal condition-type condition-message error? type-pred
          define-condition condition-of-type? handler-bind try restart-case
          invoke-restart compute-restarts find-restart restart-name restart-description
          unwind-protect defer quit exit
          call-with-otium-control call-with-native-conditions
          control-condition? control-kind control-status)
  (import (except (chezscheme) error error? condition-message exit)
          (rename (only (chezscheme) condition-message) (condition-message native-message)))

  (define handlers (make-parameter '()))
  (define restarts (make-parameter '()))
  (define parents (make-eq-hashtable))
  (define control-target (make-parameter #f))
  (define-condition-type &otium &condition make-otium-condition otium-condition?
    (value otium-value))
  (define-condition-type &otium-control &condition make-control-condition control-condition?
    (kind control-kind) (status control-status))
  (define-record-type restart (fields name description transfer))

  (define (field c key)
    (and (hashtable? c) (hashtable-ref c key #f)))
  (define (condition-type c) (field c ':type))
  (define (condition-message c) (field c ':message))
  (define (name-symbol value who)
    (cond
      [(string? value) (string->symbol value)]
      [(symbol? value)
       (let ([name (symbol->string value)])
         (if (and (> (string-length name) 0) (char=? (string-ref name 0) #\:))
             (string->symbol (substring name 1 (string-length name))) value))]
      [else (error (string-append who ": expected symbol, keyword, or string"))]))
  (define (define-condition type parent)
    (let ([type (name-symbol type "define-condition")]
          [parent (and parent (name-symbol parent "define-condition"))])
      (let loop ([ancestor parent])
        (when ancestor
          (when (eq? ancestor type) (error "define-condition: cyclic hierarchy"))
          (loop (hashtable-ref parents ancestor #f))))
      (hashtable-set! parents type parent)
      type))
  (define (condition-of-type? c type)
    (let ([type (name-symbol type "condition-of-type?")])
      (let loop ([actual (condition-type c)])
        (and (symbol? actual)
             (or (eq? actual type) (loop (hashtable-ref parents actual #f)))))))
  (define (type-pred type) (lambda (c) (condition-of-type? c type)))
  (define (error? c) (and (hashtable? c) (hashtable-contains? c ':type)))

  (define (build-condition args)
    (when (null? args) (error "error: expected a condition or message"))
    (let ([value (car args)] [args (cdr args)])
      (if (string? value)
          (let ([c (make-eq-hashtable)])
            (hashtable-set! c ':type 'error)
            (hashtable-set! c ':message value)
            (unless (null? args) (hashtable-set! c ':data (list->vector args)))
            c)
          (if (null? args) value
              (error "error: extra arguments require a string message")))))
  (define (dispatch c)
    (let loop ([frames (handlers)])
      (unless (null? frames)
        (parameterize ([handlers (cdr frames)])
          (when ((caar frames) c) ((cdar frames) c)))
        (loop (cdr frames)))))
  (define (propagate c)
    (raise
      (condition (make-otium-condition c)
        (make-message-condition
          (let ([message (condition-message c)])
            (if (string? message) message (format "~s" c))))
        (make-irritants-condition
          (let ([data (field c ':data)]) (if (vector? data) (vector->list data) '()))))))
  (define (signal . args) (dispatch (build-condition args)) #f)
  (define (error . args)
    (let ([c (build-condition args)]) (dispatch c) (propagate c)))

  (define (exception-value exception)
    (cond
      [(control-condition? exception) (raise exception)]
      [(otium-condition? exception) (otium-value exception)]
      [else
       (let ([c (if (condition? exception)
                    (build-condition
                      (cons (if (message-condition? exception)
                                (native-message exception) "Chez exception")
                            (if (irritants-condition? exception)
                                (condition-irritants exception) '())))
                    exception)])
         (dispatch c)
         c)]))
  (define (call-with-native-conditions thunk)
    (with-exception-handler (lambda (e) (propagate (exception-value e))) thunk))

  (define (call-handlers bindings thunk)
    (parameterize ([handlers (append (reverse bindings) (handlers))])
      (call-with-native-conditions thunk)))
  (define-syntax handler-bind
    (syntax-rules ()
      [(_ ((pred handler) ...) body ...)
       (handler-initializers () ((pred handler) ...) (body ...))]))
  (define-syntax handler-initializers
    (syntax-rules ()
      [(_ (bindings ...) () (body ...))
       (call-handlers (list bindings ...) (lambda () body ...))]
      [(_ (bindings ...) ((pred handler) more ...) forms)
       (let* ([p pred] [h handler])
         (handler-initializers (bindings ... (cons p h)) (more ...) forms))]))

  (define (call-try body catcher)
    ((call/cc
       (lambda (escape)
         (with-exception-handler
           (lambda (e)
             (let ([c (exception-value e)])
               (escape (lambda () (catcher c)))))
           (lambda ()
             (call-with-values body
               (lambda results (lambda () (apply values results))))))))))
  (define-syntax try
    (syntax-rules ()
      [(_ forms ...) (try-collect (forms ...) ())]))
  (define-syntax try-collect
    (lambda (stx)
      (syntax-case stx ()
        [(_ (body ... (tag (pred var) forms ...)) (clauses ...))
         (eq? (syntax->datum #'tag) 'catch)
         #'(try-collect (body ...) ((pred var forms ...) clauses ...))]
        [(_ (body ...) ((pred var forms ...) ...))
         #'(call-try (lambda () body ...)
             (lambda (c)
               (cond [(pred c) (let ([var c]) forms ...)] ...
                     [else (propagate c)])))])))

  (define (call-restarts body names descriptions clauses)
    (let ([escape #f])
      (let ([bindings
             (map (lambda (name description clause)
                    (make-restart name description
                      (lambda args (escape (lambda () (apply clause args))))))
                  names descriptions clauses)])
        (parameterize ([restarts (append (reverse bindings) (restarts))])
          ((call/cc
             (lambda (k)
               (set! escape k)
               (call-with-values body
                 (lambda results (lambda () (apply values results)))))))))))
  (define-syntax restart-case
    (syntax-rules ()
      [(_ expr clause ...) (restart-collect expr () () () clause ...)]))
  (define-syntax restart-collect
    (lambda (stx)
      (syntax-case stx ()
        [(_ expr (names ...) (descriptions ...) (clauses ...))
         #'(call-restarts (lambda () expr) '(names ...)
             (list descriptions ...) (list clauses ...))]
        [(_ expr (names ...) (descriptions ...) (clauses ...)
            (name description params body ...) more ...)
         (string? (syntax->datum #'description))
         #'(restart-collect expr (names ... name) (descriptions ... description)
             (clauses ... (lambda params body ...)) more ...)]
        [(_ expr (names ...) (descriptions ...) (clauses ...)
            (name params body ...) more ...)
         #'(restart-collect expr (names ... name) (descriptions ... #f)
             (clauses ... (lambda params body ...)) more ...)])))
  (define (compute-restarts) (list->vector (restarts)))
  (define (find-restart name)
    (if (restart? name)
        (and (memq name (restarts)) name)
        (let ([name (name-symbol name "find-restart")])
          (find (lambda (r) (eq? name (restart-name r))) (restarts)))))
  (define (invoke-restart name . args)
    (let ([r (find-restart name)])
      (if r (apply (restart-transfer r) args) (error "invoke-restart: no active restart"))))
  (define-syntax unwind-protect
    (syntax-rules ()
      [(_ expr cleanup ...)
       (dynamic-wind (lambda () #f) (lambda () expr) (lambda () #f cleanup ...))]))
  (define-syntax defer
    (syntax-rules () [(_ expr cleanup ...) (unwind-protect expr cleanup ...)]))

  (define (transfer-control kind status)
    (let ([target (control-target)])
      (if target (target kind status)
          (raise (make-control-condition kind status)))))
  (define exit
    (case-lambda
      [() (exit 0)]
      [(status)
       (unless (and (integer? status) (exact? status) (<= 0 status 255))
         (error "exit: expected integer status from 0 to 255"))
       (transfer-control 'exit status)]))
  (define quit exit)
  (define (call-with-otium-control thunk)
    (if (control-target) (thunk)
        ((call/cc
           (lambda (escape)
             (let ([previous (keyboard-interrupt-handler)])
               (parameterize
                 ([control-target
                    (lambda (kind status)
                      (escape (lambda () (raise (make-control-condition kind status)))))])
                 (dynamic-wind
                   (lambda ()
                     (keyboard-interrupt-handler
                       (lambda () (transfer-control 'interrupt 130))))
                   (lambda ()
                     (call-with-values thunk
                       (lambda results (lambda () (apply values results)))))
                   (lambda () (keyboard-interrupt-handler previous))))))))))
)
