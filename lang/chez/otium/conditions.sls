(library (otium conditions)
  (export error signal condition-type condition-message error? type-pred
          define-condition condition-of-type? handler-bind try restart-case
          invoke-restart compute-restarts find-restart restart-name restart-description
          unwind-protect defer quit exit
          call-with-otium-control call-with-native-conditions call-contained
          control-condition? control-kind control-status condition-continuation-of
          fail otium-condition? otium-value)
  (import (except (chezscheme) error error? condition-message exit)
          (prefix (only (otium truth) if) ot:)
          (prefix (otium values) val:) (otium diagnostic-catalog)
          (only (otium reader) reader-location? reader-location-code)
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
  ;; Where each table condition was raised, for locating it in source when it
  ;; is reported. Other raised values are not recorded.
  (define continuations (make-weak-eq-hashtable))
  (define (remember-continuation! c k)
    (when (val:table? c) (eq-hashtable-set! continuations c k)))
  (define (condition-continuation-of c) (eq-hashtable-ref continuations c #f))

  (define (field c key)
    (let ([keyword (val:keyword (substring (symbol->string key) 1 (string-length (symbol->string key))))])
      (if (val:table? c) (val:get c keyword) (void))))
  (define (condition-type c) (field c ':type))
  (define (condition-message c) (field c ':message))
  (define (name-symbol value who)
    (cond
      [(val:keyword? value) (string->symbol (val:keyword->string value))]
      [(string? value) (string->symbol value)]
      [(symbol? value)
       (let ([name (symbol->string value)])
         (if (and (> (string-length name) 0) (char=? (string-ref name 0) #\:))
             (string->symbol (substring name 1 (string-length name))) value))]
      [else (error (string-append who ": expected symbol, keyword, or string"))]))
  (define (define-condition type parent)
    (let ([type (name-symbol type "define-condition")]
          [parent (and parent (not (val:nil? parent)) (name-symbol parent "define-condition"))])
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
  (define (error? c)
    (and (val:table? c) (not (val:nil? (val:get c (val:keyword "type"))))))

  (define (build-condition args)
    (when (null? args) (error "error: expected a condition or message"))
    (let ([value (car args)] [args (cdr args)])
      (if (string? value)
          (let ([c (val:table (val:keyword "type") 'error (val:keyword "message") value)])
            (unless (null? args) (val:put! c (val:keyword "data") (apply val:array args)))
            c)
          (if (null? args) value
              (error "error: extra arguments require a string message")))))
  (define (dispatch c)
    (let loop ([frames (handlers)])
      (unless (null? frames)
        (parameterize ([handlers (cdr frames)])
          (ot:if ((caar frames) c) ((cdar frames) c)))
        (loop (cdr frames)))))
  ;; Mark conditions that have already visited signal-site handlers. Native
  ;; exception boundaries must not dispatch them a second time.
  (define (propagate c)
    (raise
      (condition (make-otium-condition c)
        (make-message-condition
          (let ([message (condition-message c)])
            (if (string? message) message (format "~s" c))))
        (make-irritants-condition
          (let ([data (field c ':data)]) (if (val:array? data) (val:collection-apply list data) '()))))))
  (define (signal . args) (dispatch (build-condition args)) (void))

  ;; A coded condition from lib/catalog/diagnostics.json. Fields is an
  ;; association list of symbol keys and values; each also replaces {key} in
  ;; the code's message template.
  (define (diagnostic code fields)
    (let* ([entry (or (diagnostic-entry code) (error (string-append "unknown diagnostic " code)))]
           [c (val:table (val:keyword "type") (car entry) (val:keyword "code") code)])
      (for-each (lambda (field) (val:put! c (val:keyword (symbol->string (car field))) (cdr field)))
        fields)
      (val:put! c (val:keyword "message") (fill-template (cdr entry) fields))
      c))
  (define (fill-template template fields)
    (let ([out (open-output-string)] [n (string-length template)])
      (let loop ([i 0])
        (when (< i n)
          (let ([c (string-ref template i)])
            (if (char=? c #\{)
                (let find ([j (+ i 1)])
                  (cond [(= j n) (write-char c out) (loop (+ i 1))]
                        [(char=? (string-ref template j) #\})
                         (let ([field (assq (string->symbol (substring template (+ i 1) j)) fields)])
                           (if field (display-field (cdr field) out) (display (substring template i (+ j 1)) out))
                           (loop (+ j 1)))]
                        [else (find (+ j 1))]))
                (begin (write-char c out) (loop (+ i 1)))))))
      (get-output-string out)))
  (define (display-field value port)
    (if (or (string? value) (symbol? value)) (display value port) (val:otium-write value port)))
  (define (fail code . fields)
    (let ([c (diagnostic code (let pairs ([fields fields])
                                (if (null? fields) '()
                                    (cons (cons (car fields) (cadr fields)) (pairs (cddr fields))))))])
      (call/cc (lambda (k) (remember-continuation! c k)))
      (dispatch c)
      (propagate c)))

  ;; Chez's own conditions, rewritten as Otium conditions. Known cases get a
  ;; diagnostic code; the rest keep Chez's message with its irritants filled in.
  (define (repr value) (call-with-string-output-port (lambda (port) (val:otium-write value port))))
  (define (procedure-name p)
    (let ([name (guard (e [#t #f]) (((inspect/object p) 'code) 'name))])
      (if (string? name) name "procedure")))
  (define (arity-text mask)
    (cond [(< mask 0)
           (let loop ([n 0]) (if (logbit? n mask) (format "at least ~a" n) (loop (+ n 1))))]
          [else
           (let ([counts (let loop ([n 0] [out '()])
                           (if (> (ash 1 n) mask) (reverse out)
                               (loop (+ n 1) (if (logbit? n mask) (cons n out) out))))])
             (cond [(null? counts) "no arguments"]
                   [(null? (cdr counts)) (number->string (car counts))]
                   [else (let ([text (map number->string counts)])
                           (string-append (apply string-append
                                            (map (lambda (t) (string-append t ", ")) (reverse (cdr (reverse text)))))
                             "or " (car (last-pair text))))]))]))
  ;; The operator of a printed call such as "(f 1 2)", or "function" when it is
  ;; not a plain name.
  (define (call-head text)
    (let loop ([i 1])
      (cond [(or (= i (string-length text)) (memv (string-ref text i) '(#\space #\))))
             (let ([head (substring text 1 i)])
               (if (or (string=? head "") (memv (string-ref head 0) '(#\( #\#))) "function" head))]
            [else (loop (+ i 1))])))
  ;; "a pair" from Chez's "~s is not a pair", or #f for other messages.
  (define (expected-type message)
    (let ([prefix "~s is not "] [n (string-length message)])
      (and (> n 12) (string=? (substring message 0 10) prefix)
           (let ([rest (substring message 10 n)])
             (and (not (memv #\~ (string->list rest)))
                  (or (string=? (substring rest 0 2) "a ") (string=? (substring rest 0 3) "an "))
                  rest)))))
  (define (native-condition exception)
    (let* ([message (if (message-condition? exception) (native-message exception) "Chez exception")]
           [irritants (if (irritants-condition? exception) (condition-irritants exception) '())]
           [who (and (who-condition? exception) (condition-who exception))]
           [who (and who (if (symbol? who) (symbol->string who) (and (string? who) who)))])
      (cond
        [(and (reader-location? exception) (reader-location-code exception))
         (diagnostic (reader-location-code exception) '())]
        [(and (string=? message "incorrect number of arguments ~s to ~s") (= (length irritants) 2)
              (procedure? (cadr irritants)))
         (let ([expected (arity-text (procedure-arity-mask (cadr irritants)))]
               [got (car irritants)])
           (diagnostic "E0301"
             (list (cons 'name (procedure-name (cadr irritants))) (cons 'got got)
                   (cons 'expected expected)
                   (cons 'label (format "expects ~a argument~a, called with ~a"
                                  expected (if (equal? expected "1") "" "s") got)))))]
        [(and (string=? message "incorrect argument count in call ~a") (= (length irritants) 1))
         (diagnostic "E0301"
           (list (cons 'name (call-head (format "~a" (car irritants))))
                 (cons 'call (format "~a" (car irritants)))))]
        [(and (string=? message "variable ~:s is not bound") (= (length irritants) 1)
              (symbol? (car irritants)))
         (diagnostic "E0201" (list (cons 'name (symbol->string (car irritants)))))]
        [(and (or (string=? message "attempt to apply non-procedure ~s")
                  (string=? message "expected callable value"))
              (= (length irritants) 1))
         (diagnostic "E0302" (list (cons 'value (repr (car irritants)))))]
        [(and who (= (length irritants) 1) (> (string-length message) 9)
              (string=? (substring message 0 9) "expected ")
              (not (memv #\~ (string->list message))))
         ;; Otium's own Chez libraries say (error 'who "expected ..." value).
         (diagnostic "E0401"
           (list (cons 'detail (string-append "`" who "` " message ", got " (repr (car irritants))))
                 (cons 'who who) (cons 'value (car irritants))))]
        [(and who (= (length irritants) 1) (expected-type message))
         => (lambda (expected)
              (diagnostic "E0401"
                (list (cons 'detail (string-append "`" who "` expected " expected ", got "
                                      (repr (car irritants))))
                      (cons 'who who) (cons 'value (car irritants)))))]
        [else
         (let ([c (build-condition
                    (cons (guard (e [#t message])
                            (if (format-condition? exception)
                                (apply format message (map repr-irritant irritants))
                                message))
                          (if (format-condition? exception) '() irritants)))])
           (when who (val:put! c (val:keyword "who") who))
           c)])))
  (define (repr-irritant x)
    (if (symbol? x) (string->symbol (symbol->string x)) x))
  (define (error . args)
    (let ([c (build-condition args)])
      (when (val:table? c) (call/cc (lambda (k) (remember-continuation! c k))))
      (dispatch c)
      (propagate c)))

  (define (exception-value exception)
    (cond
      [(control-condition? exception) (raise exception)]
      [(otium-condition? exception) (otium-value exception)]
      [else
       (let ([c (if (condition? exception) (native-condition exception) exception)])
         (when (continuation-condition? exception)
           (remember-continuation! c (condition-continuation exception)))
         (dispatch c)
         c)]))
  ;; Chez compiler warnings, such as a call with the wrong argument count, are
  ;; raised continuably; compiling continues and the code fails when it runs.
  (define (call-with-native-conditions thunk)
    (with-exception-handler
      (lambda (e) (if (warning? e) (void) (propagate (exception-value e))))
      thunk))

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

  ;; The escape returns a thunk so catch predicates run after body cleanup.
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
               (cond [(ot:if (pred c) #t #f) (let ([var c]) forms ...)] ...
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
         #'(restart-collect expr (names ... name) (descriptions ... (void))
             (clauses ... (lambda params body ...)) more ...)])))
  (define (compute-restarts) (apply val:array (restarts)))
  (define (find-restart name)
    (if (restart? name)
        (if (memq name (restarts)) name (void))
        (let ([name (name-symbol name "find-restart")])
          (or (find (lambda (r) (eq? name (restart-name r))) (restarts)) (void)))))
  (define (invoke-restart name . args)
    (let ([r (find-restart name)])
      (if (not (val:nil? r)) (apply (restart-transfer r) args) (error "invoke-restart: no active restart"))))
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
  ;; Run thunk so that nothing leaves it except a normal return, as needed where
  ;; native frames lie outside. Errors, exits, and other escapes are stopped
  ;; and become (contained replay), where calling replay later re-raises the
  ;; error or repeats the exit; replay is #f for an escape it cannot repeat.
  ;; Signal-site handlers have already run for a contained error.
  (define (call-contained thunk contained)
    ((call/cc
       (lambda (k)
         (let ([leaving #f])
           (define (contain replay)
             (set! leaving #t)
             (k (lambda () (contained replay))))
           (dynamic-wind
             (lambda () #f)
             (lambda ()
               (parameterize
                 ([control-target
                    (lambda (kind status) (contain (lambda () (transfer-control kind status))))])
                 (with-exception-handler
                   (lambda (e)
                     (if (control-condition? e)
                         (contain (lambda () (raise e)))
                         (let ([c (exception-value e)]) (contain (lambda () (propagate c))))))
                   (lambda ()
                     (call-with-values thunk
                       (lambda results
                         (set! leaving #t)
                         (lambda () (apply values results))))))))
             (lambda () (unless leaving (contain #f)))))))))
  ;; Escape before raising the host notification, bypassing even native guards
  ;; inside Otium code while still running dynamic-wind cleanup.
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
  ;; Condition types of the diagnostic catalog. All are errors.
  (for-each (lambda (type) (hashtable-set! parents type 'error))
    '(read-error syntax-error name-error namespace-error arity-error type-error)))
