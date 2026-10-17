(library (otium runtime)
  (export evaluate load-source require-namespace current-namespace namespace-name
          load-path namespace-ref namespace-set! repl)
  (import (chezscheme) (prefix (otium generic-core) gm:)
          (prefix (otium conditions) ot:) (otium expander) (otium line-edit)
          (otium runtime-documentation))

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
        (let* ([env (copy-environment
                      (environment '(except (chezscheme) error error? condition-message exit)
                        '(only (otium conditions)
                           error signal condition-type condition-message error? type-pred
                           define-condition condition-of-type? handler-bind try restart-case
                           invoke-restart compute-restarts find-restart restart-name
                           restart-description unwind-protect defer quit exit)))]
               [space (make-namespace name env (make-eq-hashtable)
                        (make-eq-hashtable) #f #f #f)])
          (hashtable-set! namespaces name space)
          (define-top-level-value 'chez/error error env)
          (define-top-level-value 'chez/condition-message condition-message env)
          (define-top-level-value 'describe
            (lambda (name)
              (unless (symbol? name) (error 'describe "expected a symbol" name))
              (binding-documentation space name)) env)
          (define-top-level-value 'macro? otium-macro? env)
          ;; Native syntax may preserve nested forms past the Otium traversal.
          ;; Reject declarations there too; lexical value bindings still shadow them.
          (for-each
            (lambda (name)
              (define-top-level-syntax name
                (lambda (form)
                  (gm:fail 'macroexpand 'invalid-declaration
                    "declaration requires top-level context" (syntax->datum form))) env))
            '(define-type define-generic define-method reset-type! reset-generic!))
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
          (when (eq? name 'otium.generic)
            (for-each
              (lambda (entry)
                (define-top-level-value (car entry) (cdr entry) env)
                (hashtable-set! (namespace-definitions space) (car entry)
                  "Typed maps and generic dispatch.")) gm:public-bindings)
            (namespace-loaded?-set! space #t))
          (when (eq? name 'otium.ffi)
            (for-each
              (lambda (entry)
                (define-top-level-value (car entry) (cdr entry) env)
                (hashtable-set! (namespace-definitions space) (car entry)
                  "Portable foreign calls and native memory."))
              (eval 'public-bindings (environment '(otium ffi))))
            (namespace-loaded?-set! space #t))
          (when (memq name '(otium.process otium.cli))
            (for-each
              (lambda (entry)
                (define-top-level-value (car entry) (cdr entry) env)
                (hashtable-set! (namespace-definitions space) (car entry)
                  "Portable command-line programs."))
              (eval 'public-bindings
                (environment (if (eq? name 'otium.cli) '(otium cli) '(otium process)))))
            (namespace-loaded?-set! space #t))
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
      (when (and (not (memq name '(otium.generic otium.ffi otium.process otium.cli)))
                 (or reload? (not (namespace-loaded? space))))
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

  (define (generic-declaration! space form)
    (define (invalid message)
      (gm:fail (car form) 'invalid-declaration message form))
    (define (identifier? x)
      (and (symbol? x) (not (memv #\/ (string->list (symbol->string x))))))
    (define (parameters! parameters specialized?)
      (unless (list? parameters) (invalid "expected fixed parameter list"))
      (let ([names (map (lambda (p)
                         (cond [(symbol? p) p]
                               [(and specialized? (list? p) (= (length p) 2)
                                     (symbol? (car p)) (symbol? (cadr p))) (car p)]
                               [else (invalid "invalid parameter")])) parameters)])
        (let loop ([rest names])
          (unless (null? rest)
            (when (memq (car rest) (cdr rest)) (invalid "duplicate parameter"))
            (loop (cdr rest))))
        names))
    (define env (namespace-environment space))
    (define kind (car form))
    (unless (and (list? form) (>= (length form) 2))
      (invalid "malformed declaration"))
    (if (eq? kind 'define-method)
        (let ([head (cadr form)])
          (unless (and (pair? head) (list? head) (symbol? (car head))
                       (>= (length form) 3))
            (invalid "expected (define-method (generic parameters ...) body ...)"))
          (let* ([names (parameters! (cdr head) #t)]
                 [generic (eval (car head) env)])
            (unless (gm:generic? generic)
              (gm:fail kind 'not-generic "expected generic procedure" (car head)))
            (unless (= (length names) (gm:generic-arity generic))
              (gm:fail kind 'wrong-arity "method arity differs from generic" (car head)))
            (let* ([signature
                    (map (lambda (p)
                           (and (pair? p)
                                (let ([type (eval (cadr p) env)])
                                  (unless (gm:descriptor? type)
                                    (gm:fail kind 'invalid-specializer
                                      "expected type descriptor" (cadr p)))
                                  type))) (cdr head))]
                   [implementation
                    (eval ((expander-expression (namespace-expander space))
                           `(lambda ,names ,@(cddr form)) '()) env)])
              (gm:add-method! generic signature implementation))))
        (let* ([type? (memq kind '(define-type reset-type!))]
               [reset? (memq kind '(reset-type! reset-generic!))]
               [head (cadr form)]
               [name (if type? head (and (pair? head) (car head)))])
          (unless (and (= (length form) 2) (identifier? name))
            (invalid "expected local declaration name"))
          (let* ([arity (and (not type?) (length (parameters! (cdr head) #f)))]
                 [owner (cons (namespace-name space) name)]
                 [defined? (hashtable-contains? (namespace-definitions space) name)]
                 [bound? (top-level-bound? name env)])
            (when (or (eq? (namespace-name space) 'otium.generic)
                      (hashtable-contains? (namespace-imports space) name)
                      (and reset? (not defined?))
                      (and (not reset?) (not defined?)
                           (or bound? (memq name (environment-symbols env)))))
              (gm:fail kind 'binding-conflict "declaration would replace another binding" name))
            (cond
              [(and defined? (not reset?))
               (let ([old (and bound? (top-level-value name env))])
                 (unless (if type?
                             (and (gm:descriptor? old) (equal? owner (gm:descriptor-owner old)))
                             (and (gm:generic? old) (equal? owner (gm:generic-owner old))
                                  (= arity (gm:generic-arity old))))
                   (gm:fail kind 'binding-conflict
                     "incompatible declaration; use explicit reset to replace it" name)))]
              [else
               (define-top-level-value name
                 (if type? (gm:fresh-type (namespace-name space) name)
                     (gm:fresh-generic (namespace-name space) name arity)) env)
               (hashtable-set! (namespace-definitions space) name
                 (if type? "User-defined dispatch type." "Generic procedure."))])))))

  (define (evaluate form)
    (ot:call-with-otium-control
      (lambda ()
        (ot:call-with-native-conditions
          (lambda () (with-expansion (lambda () (evaluate-top form))))))))

  (define (evaluate-top input)
    (let* ([space (current-namespace)] [env (namespace-environment space)]
           [expander (namespace-expander space)]
           [form ((expander-head expander) input '() #f)])
      (cond
        [(and (pair? form)
              (memq (car form) '(define-type define-generic define-method reset-type! reset-generic!)))
         (generic-declaration! space form)]
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
         (unless (and (list? form) (<= 3 (length form) 4) (symbol? (cadr form))
                      (or (= (length form) 3) (string? (caddr form))))
           (error 'defonce "expected (defonce name [docstring] expression)" form))
         (unless (hashtable-contains? (namespace-definitions space) (cadr form))
           (evaluate `(define ,@(cdr form))))]
        [(and (pair? form) (eq? (car form) 'defmacro))
         (unless (and (list? form) (>= (length form) 4) (symbol? (cadr form)))
           (error 'defmacro "expected (defmacro name parameters body ...)" form))
         (when (expander-core-form? (cadr form))
           (error 'defmacro "cannot replace a core form" (cadr form)))
         (when (hashtable-contains? (namespace-imports space) (cadr form))
           (error 'defmacro "definition would replace an import" (cadr form)))
         (let ([transformer
                (eval ((expander-expression expander)
                        `(lambda ,(caddr form)
                           ,@(let ([body (cdddr form)])
                               (if (and (string? (car body)) (pair? (cdr body)))
                                   (cdr body) body))) '()) env)])
           (define-top-level-value (cadr form)
             (make-otium-macro transformer space (cadr form)) env)
           (hashtable-set! (namespace-definitions space) (cadr form) (definition-help form)))]
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
                 (hashtable-set! (namespace-definitions space) definition (definition-help form)))
               (apply values results))))])))

  (define (definition-help form)
    (let* ([macro? (eq? (car form) 'defmacro)]
           [head (cadr form)]
           [parameters (cond [macro? (caddr form)] [(pair? head) (cdr head)] [else #f])]
           [body (if macro? (cdddr form) (cddr form))]
           [doc (if (and (pair? body) (string? (car body)) (pair? (cdr body)))
                    (car body) "")])
      (string-append
        (if parameters
            (format "~a parameters: ~s. " (if macro? "Macro" "Procedure") parameters)
            "")
        doc)))

  (define (binding-documentation space name)
    (let* ([import (hashtable-ref (namespace-imports space) name #f)]
           [owner (if import (find-namespace (car import)) space)]
           [member (if import (cdr import) name)]
           [definitions (namespace-definitions owner)])
      (if (hashtable-contains? definitions member)
          (hashtable-ref definitions member "")
          (runtime-documentation member))))

  (define (repl-bindings)
    (let ([space (current-namespace)])
      (map (lambda (name)
             (cons (symbol->string name) (binding-documentation space name)))
           (append '(ns require defonce defmacro)
                   (environment-symbols (namespace-environment space))))))

  (define (repl)
    (let* ([editing? (line-editor-available?)]
           [continuation? #f]
           [prompt (lambda ()
                     (let ([text (if continuation? "..> "
                                     (string-append
                                       (symbol->string (namespace-name (current-namespace)))
                                       "> "))])
                       (set! continuation? #t)
                       text))]
           [input (if editing? (make-line-input-port prompt repl-bindings) (current-input-port))])
      (when editing?
        (display "otium repl - ^C interrupts, ^D exits, ^X ^E opens $EDITOR, Tab completes, ,help for help\n"))
      (dynamic-wind
        (lambda () (void))
        (lambda ()
          (let loop ()
            (set! continuation? #f)
            (unless editing?
              (display (namespace-name (current-namespace)))
              (display "> ")
              (flush-output-port (current-output-port)))
            (let ([continue?
                   (guard (c [(and (ot:control-condition? c) (eq? (ot:control-kind c) 'exit))
                              (raise c)]
                             [else
                              (display-condition c (current-error-port))
                              (newline (current-error-port))
                              (when editing?
                                (close-port input)
                                (set! input (make-line-input-port prompt repl-bindings)))
                              #t])
                     (let ([form (read input)])
                       (if (eof-object? form) #f
                           (begin
                             (call-with-values (lambda () (evaluate form))
                               (lambda values
                                 (for-each
                                   (lambda (v) (unless (eq? v (void)) (pretty-print v)))
                                   values)))
                             #t))))])
              (when continue? (loop)))))
        (lambda ()
          (when editing? (close-port input) (line-editor-close))))))

  (current-namespace (find-namespace 'user)))
