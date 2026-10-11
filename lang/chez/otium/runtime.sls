(library (otium runtime)
  (export evaluate load-source require-namespace current-namespace namespace-name
          load-path namespace-ref namespace-set! repl)
  (import (chezscheme) (otium expander))

  (define-record-type otium-macro
    (fields transformer namespace name))

  (define-record-type namespace
    (fields name environment imports definitions
            (mutable loaded?) (mutable loading?) (mutable source)))
  (define namespaces (make-eq-hashtable))
  (define current-namespace (make-parameter #f))
  (define load-path (make-parameter '(".")))
  (define source-path (make-parameter #f))
  (define expansion-budget (make-parameter #f))

  (define (with-expansion thunk)
    (if (expansion-budget) (thunk)
        (parameterize ([expansion-budget (vector 10000 '())]) (thunk))))

  (define (binding-value space name)
    (let ([import (hashtable-ref (namespace-imports space) name #f)])
      (if import
          (namespace-ref (car import) (cdr import))
          (let ([env (namespace-environment space)])
            (and (top-level-bound? name env) (top-level-value name env))))))

  (define (namespace-expander space)
    (make-expander
      (lambda (name)
        (let ([value (binding-value space name)])
          (and (otium-macro? value) value)))
      (lambda (name)
        (and (not (hashtable-contains? (namespace-imports space) name))
             (not (top-level-bound? name (namespace-environment space)))
             (top-level-syntax? name (namespace-environment space))))
      (lambda (macro args)
        (let ([budget (expansion-budget)])
          (when (= (vector-ref budget 0) 0)
            (error 'macroexpand "expansion limit exceeded"
              (otium-macro-name macro) (namespace-name space) (source-path)
              (reverse (vector-ref budget 1))))
          (vector-set! budget 0 (- (vector-ref budget 0) 1))
          (vector-set! budget 1
            (cons (otium-macro-name macro)
              (let take ([names (vector-ref budget 1)] [n 11])
                (if (or (null? names) (= n 0)) '()
                    (cons (car names) (take (cdr names) (- n 1))))))))
        (guard (c [else
                   (raise
                     (condition
                       (make-who-condition 'macroexpand)
                       (make-message-condition "macro transformer failed")
                       (make-irritants-condition
                         (list (otium-macro-name macro) (namespace-name space)
                               (source-path)
                               (reverse (vector-ref (expansion-budget) 1))
                               (call-with-string-output-port
                                 (lambda (port) (display-condition c port)))))))])
          (parameterize ([current-namespace (otium-macro-namespace macro)])
            (call-with-values
              (lambda () (apply (otium-macro-transformer macro) args))
              (lambda results
                (unless (= (length results) 1)
                  (error 'defmacro "transformer must return one datum" results))
                (car results))))))))

  (define (namespace-ref name member)
    (top-level-value member (namespace-environment (find-namespace name))))

  (define (namespace-set! name member value)
    (set-top-level-value! member value
      (namespace-environment (find-namespace name))))

  (define (find-namespace name)
    (unless (symbol? name) (error 'ns "expected a symbol" name))
    (or (hashtable-ref namespaces name #f)
        (let* ([env (copy-environment (scheme-environment))]
               [space (make-namespace name env (make-eq-hashtable)
                        (make-eq-hashtable) #f #f #f)])
          (hashtable-set! namespaces name space)
          (define-top-level-value 'macro? otium-macro? env)
          (for-each
            (lambda (entry)
              (define-top-level-value (car entry)
                (lambda (form)
                  (parameterize ([current-namespace space])
                    (with-expansion
                      (lambda ()
                        ((expander-head (namespace-expander space))
                          form '() (cdr entry)))))) env))
            '((macroexpand-1 . #t) (macroexpand . #f)))
          (define-top-level-value 'eval
            (lambda (form)
              (parameterize ([current-namespace space]) (evaluate form))) env)
          ;; Capture the defining namespace, including when live is called later.
          (define-top-level-value 'live
            (lambda (name)
              (unless (symbol? name) (error 'live "expected a binding name" name))
              (lambda args
                (let ([import (hashtable-ref (namespace-imports space) name #f)])
                  (apply (if import
                             (namespace-ref (car import) (cdr import))
                             (top-level-value name env))
                    args))))
            env)
          space)))

  (define (import-value! space local target member)
    (unless (hashtable-contains? (namespace-definitions target) member)
      (error 'require "namespace does not define this name"
        (namespace-name target) member))
    (unless (top-level-bound? member (namespace-environment target))
      (error 'require "syntax imports are not supported yet" member))
    (when (hashtable-contains? (namespace-definitions space) local)
      (error 'require "import would replace a local definition" local))
    (let ([target-name (namespace-name target)])
      (define-top-level-syntax local
        (make-variable-transformer
          (lambda (form)
            (with-syntax ([target (datum->syntax #'anchor target-name)]
                          [member (datum->syntax #'anchor member)])
              (syntax-case form (set!)
                [(set! _ value) #'(namespace-set! 'target 'member value)]
                [(_ . args) #'((namespace-ref 'target 'member) . args)]
                [id (identifier? #'id) #'(namespace-ref 'target 'member)]))))
        (namespace-environment space))
      (hashtable-set! (namespace-imports space) local (cons target-name member))))

  (define (source-for name)
    (let ([relative (string-append
                      (list->string
                        (map (lambda (c) (if (char=? c #\.) #\/ c))
                          (string->list (symbol->string name))))
                      ".scm")])
      (let loop ([paths (load-path)])
        (if (null? paths)
            (error 'require "namespace source not found" name (load-path))
            (let ([path (string-append (car paths) "/" relative)])
              (if (file-exists? path) path (loop (cdr paths))))))))

  (define (read-source path)
    (parameterize ([source-path path])
      (call-with-input-file path
        (lambda (port)
          (let loop ()
            (let ([form (read port)])
              (unless (eof-object? form)
                (evaluate form)
                (loop))))))))

  (define (load-namespace! space path)
    (when (namespace-loading? space)
      (error 'require "circular namespace load" (namespace-name space)))
    (dynamic-wind
      (lambda () (namespace-loading?-set! space #t))
      (lambda ()
        (parameterize ([current-namespace space])
          (read-source path))
        (namespace-source-set! space path)
        (namespace-loaded?-set! space #t))
      (lambda () (namespace-loading?-set! space #f))))

  (define (require-namespace name reload?)
    (let ([space (find-namespace name)])
      (when (namespace-loading? space)
        (error 'require "circular namespace load" name))
      (when (or reload? (not (namespace-loaded? space)))
        (load-namespace! space (or (namespace-source space) (source-for name))))
      space))

  (define (load-source path)
    ;; A script may switch namespaces without changing its caller's namespace.
    (parameterize ([current-namespace (current-namespace)])
      (read-source path)))

  (define (require-form args)
    (unless (and (list? args) (pair? args) (symbol? (car args)))
      (error 'require "expected (require namespace [:as alias] [:refer (names)] [:reload])"))
    (let ([name (car args)] [space (current-namespace)])
      (let parse ([rest (cdr args)] [alias (car args)] [refers '()] [reload? #f])
        (cond
          [(null? rest)
           (let ([target (require-namespace name reload?)])
             (for-each
               (lambda (member)
                 (when (top-level-bound? member (namespace-environment target))
                   (import-value! space
                     (string->symbol (string-append (symbol->string alias) "/"
                                       (symbol->string member)))
                     target member)))
               (vector->list (hashtable-keys (namespace-definitions target))))
             (for-each (lambda (member) (import-value! space member target member)) refers))]
          [(eq? (car rest) ':reload) (parse (cdr rest) alias refers #t)]
          [(and (eq? (car rest) ':as) (pair? (cdr rest)) (symbol? (cadr rest)))
           (parse (cddr rest) (cadr rest) refers reload?)]
          [(and (eq? (car rest) ':refer) (pair? (cdr rest))
                (list? (cadr rest)) (for-all symbol? (cadr rest)))
           (parse (cddr rest) alias (cadr rest) reload?)]
          [else (error 'require "invalid options" rest)]))))

  (define (evaluate form)
    (with-expansion (lambda () (evaluate-top form))))

  (define (evaluate-top input)
    (let* ([space (current-namespace)] [env (namespace-environment space)]
           [expander (namespace-expander space)]
           [form ((expander-head expander) input '() #f)])
      (cond
        [(and (pair? form) (eq? (car form) 'ns))
         (unless (and (list? form) (= (length form) 2) (symbol? (cadr form)))
           (error 'ns "expected (ns name)" form))
         (when (and (namespace-loading? space) (not (eq? (cadr form) (namespace-name space))))
           (error 'ns "source declares a different namespace" (cadr form)))
         (current-namespace (find-namespace (cadr form)))]
        [(and (pair? form) (eq? (car form) 'require)) (require-form (cdr form))]
        [(and (pair? form) (eq? (car form) 'begin))
         (unless (list? form) (error 'begin "expected a proper list" form))
         (let loop ([forms (cdr form)])
           (cond [(null? forms) (void)]
                 [(null? (cdr forms)) (evaluate (car forms))]
                 [else (evaluate (car forms)) (loop (cdr forms))]))]
        [(and (pair? form) (eq? (car form) 'defonce))
         (unless (and (list? form) (= (length form) 3) (symbol? (cadr form)))
           (error 'defonce "expected (defonce name expression)" form))
         (unless (hashtable-contains? (namespace-definitions space) (cadr form))
           (evaluate `(define ,(cadr form) ,(caddr form))))]
        [(and (pair? form) (eq? (car form) 'defmacro))
         (unless (and (list? form) (>= (length form) 4) (symbol? (cadr form)))
           (error 'defmacro "expected (defmacro name parameters body ...)" form))
         (when (expander-core-form? (cadr form))
           (error 'defmacro "cannot replace a core form" (cadr form)))
         (when (hashtable-contains? (namespace-imports space) (cadr form))
           (error 'defmacro "definition would replace an import" (cadr form)))
         (let ([transformer
                (eval ((expander-expression expander)
                        `(lambda ,(caddr form) ,@(cdddr form)) '()) env)])
           (define-top-level-value (cadr form)
             (make-otium-macro transformer space (cadr form)) env)
           (hashtable-set! (namespace-definitions space) (cadr form) #t))]
        [else
         (let ([definition
                (and (pair? form) (memq (car form) '(define define-syntax))
                     (pair? (cdr form))
                     (if (pair? (cadr form)) (caadr form) (cadr form)))])
           (when (and definition (hashtable-contains? (namespace-imports space) definition))
             (error 'define "definition would replace an import" definition))
           (call-with-values
             (lambda ()
               (eval
                 (cond
                   [(and (pair? form) (eq? (car form) 'define))
                    ((expander-definition expander) form '())]
                   [else ((expander-expression expander) form '())]) env))
             (lambda results
               (when definition
                 (hashtable-set! (namespace-definitions space) definition #t))
               (apply values results))))])))

  (define (repl)
    (let loop ()
      (display (namespace-name (current-namespace)))
      (display "> ")
      (flush-output-port (current-output-port))
      (let ([continue?
             (guard (c [else (display-condition c (current-error-port))
                            (newline (current-error-port)) #t])
               (let ([form (read)])
                 (if (eof-object? form) #f
                     (begin
                       (call-with-values (lambda () (evaluate form))
                         (lambda values
                           (for-each (lambda (v) (unless (eq? v (void)) (pretty-print v)))
                             values)))
                       #t))))])
        (when continue? (loop)))))

  (current-namespace (find-namespace 'user)))
