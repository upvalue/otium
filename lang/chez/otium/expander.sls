(library (otium expander)
  (export make-expander expander-head expander-expression expander-definition
          expander-core-form?)
  (import (chezscheme))

  (define-record-type (expander make-expander-record expander?)
    (fields head expression definition))

  (define core-forms
    '(quote quasiquote unquote unquote-splicing if begin lambda define set!
      let let* letrec letrec* and or cond defmacro ns require defonce
      handler-bind try restart-case unwind-protect defer
      define-type define-generic define-method reset-type! reset-generic!))

  (define (expander-core-form? name) (and (memq name core-forms) #t))

  (define (shape! form minimum maximum)
    (unless (and (list? form) (>= (length form) minimum)
                 (or (not maximum) (<= (length form) maximum)))
      (error 'macroexpand "malformed form" form)))

  (define (parameters names)
    (let walk ([names names] [seen '()])
      (cond
        [(null? names) seen]
        [(symbol? names)
         (when (memq names seen) (error 'lambda "duplicate parameter" names))
         (cons names seen)]
        [(and (pair? names) (symbol? (car names)))
         (when (memq (car names) seen)
           (error 'lambda "duplicate parameter" (car names)))
         (walk (cdr names) (cons (car names) seen))]
        [else (error 'lambda "invalid parameters" names)])))

  (define (definition-name form)
    (shape! form 3 #f)
    (let ([target (cadr form)])
      (cond [(symbol? target) (shape! form 3 4) target]
            [(and (pair? target) (symbol? (car target))) (car target)]
            [else (error 'define "invalid definition" form)])))

  (define (make-expander lookup native-syntax? invoke)
    (define (head form scope once?)
      (if (and (pair? form) (symbol? (car form))
               (not (memq (car form) core-forms))
               (not (memq (car form) scope)))
          (let ([macro (lookup (car form))])
            (if macro
                (begin
                  (shape! form 1 #f)
                  (let ([result (invoke macro (cdr form))])
                    (if once? result (head result scope #f))))
                form))
          form))

    (define (expressions forms scope)
      (map (lambda (form) (expression form scope)) forms))

    (define (definition form scope)
      (let ([name (definition-name form)])
        (if (pair? (cadr form))
            (cons 'define
              (cons (cadr form)
                (body (let ([forms (cddr form)])
                        (if (and (pair? forms) (string? (car forms)) (pair? (cdr forms)))
                            (cdr forms) forms))
                  (append (parameters (cdadr form)) (cons name scope)))))
            (let ([values (cddr form)])
              (when (and (pair? values) (string? (car values)) (pair? (cdr values)))
                (set! values (cdr values)))
              (unless (and (pair? values) (null? (cdr values)))
                (error 'define "expected one value expression after optional docstring" form))
              `(define ,name ,(expression (car values) (cons name scope)))))))

    (define (body forms scope)
      ;; Chez internal definitions bind throughout the body, including earlier
      ;; initializers. Discover literal definitions before invoking transformers.
      (let collect ([rest forms] [definitions '()])
        (cond
          [(null? rest) (error 'macroexpand "body needs an expression" forms)]
          [(and (pair? (car rest)) (eq? (caar rest) 'begin)
                (not (memq 'begin scope)))
           (shape! (car rest) 1 #f)
           (collect (append (cdar rest) (cdr rest)) definitions)]
          [(and (pair? (car rest)) (eq? (caar rest) 'define)
                (not (memq 'define scope)))
           (definition-name (car rest))
           (collect (cdr rest) (cons (car rest) definitions))]
          [else
           (let* ([definitions (reverse definitions)]
                  [scope (append (map definition-name definitions) scope)])
             (append (map (lambda (form) (definition form scope)) definitions)
                     (expressions rest scope)))])))

    (define (bindings form scope)
      (let* ([kind (car form)]
             [named? (and (eq? kind 'let) (symbol? (cadr form)))]
             [name (and named? (cadr form))])
        (when named? (shape! form 4 #f))
        (let ([bs (if named? (caddr form) (cadr form))]
              [forms (if named? (cdddr form) (cddr form))])
          (unless (and (list? bs)
                       (for-all (lambda (b)
                                  (and (list? b) (= (length b) 2)
                                       (symbol? (car b)))) bs))
            (error 'macroexpand "invalid bindings" form))
          (let* ([names (map car bs)]
                 [body-scope (append names (if named? (cons name scope) scope))]
                 [initial-scope (if (memq kind '(letrec letrec*)) body-scope scope)]
                 [expanded
                  (let loop ([bs bs] [scope initial-scope])
                    (if (null? bs) '()
                        (cons (list (caar bs) (expression (cadar bs) scope))
                              (loop (cdr bs)
                                (if (eq? kind 'let*) (cons (caar bs) scope) scope)))))])
            (append (if named? (list kind name expanded) (list kind expanded))
                    (body forms body-scope))))))

    (define (quasiquoted datum depth scope)
      (cond
        [(vector? datum)
         (list->vector (map (lambda (x) (quasiquoted x depth scope))
                           (vector->list datum)))]
        [(pair? datum)
         (case (car datum)
           [(quasiquote)
            (shape! datum 2 2)
            (list 'quasiquote (quasiquoted (cadr datum) (+ depth 1) scope))]
           [(unquote unquote-splicing)
            (shape! datum 2 2)
            (list (car datum)
              (if (= depth 1) (expression (cadr datum) scope)
                  (quasiquoted (cadr datum) (- depth 1) scope)))]
           [else (cons (quasiquoted (car datum) depth scope)
                       (quasiquoted (cdr datum) depth scope))])]
        [else datum]))

    (define (conditional clauses scope)
      (map
        (lambda (clause)
          (shape! clause 1 #f)
          (cond
            [(eq? (car clause) 'else)
             (cons 'else (expressions (cdr clause) scope))]
            [(and (pair? (cdr clause)) (eq? (cadr clause) '=>))
             (shape! clause 3 3)
             (list (expression (car clause) scope) '=>
                   (expression (caddr clause) scope))]
            [else (expressions clause scope)]))
        clauses))

    (define (handler-form form scope)
      (shape! form 3 #f)
      (unless (list? (cadr form)) (error 'handler-bind "invalid bindings" form))
      (cons 'handler-bind
        (cons (map (lambda (binding)
                     (shape! binding 2 2)
                     (expressions binding scope)) (cadr form))
              (body (cddr form) scope))))

    (define (try-form form scope)
      (let split ([rest (reverse (cdr form))] [clauses '()])
        (if (and (pair? rest) (pair? (car rest)) (eq? (caar rest) 'catch))
            (let ([clause (car rest)])
              (shape! clause 3 #f)
              (shape! (cadr clause) 2 2)
              (unless (symbol? (cadadr clause)) (error 'try "invalid catch variable" clause))
              (split (cdr rest)
                (cons `(catch (,(expression (caadr clause) scope) ,(cadadr clause))
                         ,@(body (cddr clause) (cons (cadadr clause) scope))) clauses)))
            (begin
              (when (exists (lambda (x) (and (pair? x) (eq? (car x) 'catch))) rest)
                (error 'try "catch clauses must follow the body" form))
              (cons 'try (append (body (reverse rest) scope) clauses))))))

    (define (restart-form form scope)
      (shape! form 2 #f)
      (cons 'restart-case
        (cons (expression (cadr form) scope)
          (map (lambda (clause)
                 (shape! clause 3 #f)
                 (unless (symbol? (car clause)) (error 'restart-case "invalid name" clause))
                 (let* ([description? (string? (cadr clause))]
                        [rest (if description? (cddr clause) (cdr clause))])
                   (when description? (shape! clause 4 #f))
                   (append (if description? (list (car clause) (cadr clause)) (list (car clause)))
                     (list (car rest))
                     (body (cdr rest) (append (parameters (car rest)) scope)))))
               (cddr form)))))

    (define (expression input scope)
      (let ([form (head input scope #f)])
        (if (not (pair? form)) form
            (let ([operator (car form)])
              (shape! form 1 #f)
              (if (and (symbol? operator) (memq operator scope))
                  (expressions form scope)
                  (case operator
                [(quote) (shape! form 2 2) form]
                [(quasiquote)
                 (shape! form 2 2)
                 (list 'quasiquote (quasiquoted (cadr form) 1 scope))]
                [(unquote unquote-splicing)
                 (error 'macroexpand "unquote outside quasiquote" form)]
                [(lambda)
                 (shape! form 3 #f)
                 (cons 'lambda
                   (cons (cadr form)
                     (body (cddr form) (append (parameters (cadr form)) scope))))]
                [(let let* letrec letrec*)
                 (shape! form 3 #f)
                 (bindings form scope)]
                [(if)
                 (shape! form 3 4)
                 (cons operator (expressions (cdr form) scope))]
                [(begin and or)
                 (cons operator (expressions (cdr form) scope))]
                [(set!)
                 (shape! form 3 3)
                 (unless (symbol? (cadr form)) (error 'set! "expected name" form))
                 (list 'set! (cadr form) (expression (caddr form) scope))]
                [(handler-bind) (handler-form form scope)]
                [(try) (try-form form scope)]
                [(restart-case) (restart-form form scope)]
                [(unwind-protect defer)
                 (shape! form 2 #f)
                 (cons operator (expressions (cdr form) scope))]
                [(cond) (cons 'cond (conditional (cdr form) scope))]
                [(define)
                 (error 'macroexpand
                   "generated internal definitions and definitions in expression context are unsupported"
                   form)]
                [(define-type define-generic define-method reset-type! reset-generic!)
                 (error 'macroexpand "declaration requires top-level context"
                   'invalid-declaration form)]
                [(ns require defonce defmacro)
                 (error 'macroexpand "directive requires top-level context" form)]
                [else
                 ;; Until native syntax gets an explicit boundary, preserve its
                 ;; grammar intact. Its operands are not necessarily expressions.
                 (if (and (symbol? operator) (not (memq operator scope))
                          (native-syntax? operator))
                     form
                     (expressions form scope))]))))))

    (make-expander-record head expression definition)))
