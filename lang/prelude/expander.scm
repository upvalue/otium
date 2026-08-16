;; expander.scm -- the Otium macroexpander, written in the macro-free subset.
;;
;; Loaded by the stage-0 evaluator BEFORE any macro exists, so this file may
;; use only special forms (quote if define set! lambda begin let while and or
;; cond quasiquote) and natives.  No when/unless/defn/-> anywhere here.
;;
;; Scope is tracked in Otium data (a plain list of symbols) -- no native
;; lexical oracle is needed.  The single native oracle used:
;;   (expander-macro-var sym) -> the macro object if sym resolves as a var
;;                               to a macro in the current namespace, else nil.

(define expander-special-forms
  '(quote if define def define- set! lambda fn defmacro begin do let while
    and or cond quasiquote unquote unquote-splicing ns in-ns require
    handler-bind restart-case try unwind-protect defer defparam with-params))

(define (exp-memq sym lst)
  (if (pair? lst)
      (if (eq? sym (car lst)) #t (exp-memq sym (cdr lst)))
      #f))

(define (exp-special? sym) (exp-memq sym expander-special-forms))

;; --- parameter lists -> the symbols they bind -------------------------------

(define (exp-param-walk p)
  (cond ((null? p) '())
        ((symbol? p) (list p))                       ; dotted rest tail
        ((pair? p)
         (if (eq? (car p) '&)
             (exp-param-walk (cdr p))
             (cons (car p) (exp-param-walk (cdr p)))))
        (else '())))

(define (exp-param-names params)
  (cond ((symbol? params) (list params))             ; bare rest symbol
        ((pair? params)
         (exp-param-walk
          (if (eq? (car params) 'array) (cdr params) params))) ; [a b] spelling
        (else '())))

;; --- generic walkers --------------------------------------------------------

;; Expand every element of a (possibly improper) list.
(define (exp-list forms scope)
  (cond ((pair? forms)
         (cons (exp-form (car forms) scope) (exp-list (cdr forms) scope)))
        (else forms)))

;; Expand a body sequence, tracking body-level define/def/define- so that a
;; locally defined name shadows macros in head position for later forms.
(define (exp-define-head? s)
  (or (eq? s 'define) (eq? s 'def) (eq? s 'define-)))

(define (exp-defined-name f)
  (let ((t (car (cdr f))))
    (if (pair? t) (car t) t)))

(define (exp-body forms scope)
  (if (pair? forms)
      (let ((f (car forms)))
        (let ((scope2 (if (and (pair? f) (exp-define-head? (car f)))
                          (cons (exp-defined-name f) scope)
                          scope)))
          (cons (exp-form f scope2) (exp-body (cdr forms) scope2))))
      forms))

;; ((a b) ...) with both positions expanded (handler-bind / with-params).
(define (exp-pair-list bs scope)
  (if (pair? bs)
      (cons (exp-list (car bs) scope) (exp-pair-list (cdr bs) scope))
      '()))

;; --- special-form walkers ---------------------------------------------------

;; let bindings are sequential; returns (expanded-bindings . new-scope).
(define (exp-let-bindings bs scope)
  (if (pair? bs)
      (let ((b (car bs)))
        (let ((name (car b))
              (ex (exp-form (car (cdr b)) scope)))
          (let ((r (exp-let-bindings (cdr bs) (cons name scope))))
            (cons (cons (list name ex) (car r)) (cdr r)))))
      (cons '() scope)))

(define (exp-let form scope)
  (let ((r (exp-let-bindings (car (cdr form)) scope)))
    (cons 'let (cons (car r) (exp-body (cdr (cdr form)) (cdr r))))))

(define (exp-lambda form scope)
  (let ((params (car (cdr form))))
    (cons (car form)
          (cons params
                (exp-body (cdr (cdr form))
                          (append (exp-param-names params) scope))))))

(define (exp-define form scope)
  (let ((target (car (cdr form))))
    (if (pair? target)
        ;; (define (name . params) body...) -- name and params are data.
        (cons (car form)
              (cons target
                    (exp-body (cdr (cdr form))
                              (append (exp-param-names (cdr target))
                                      (cons (car target) scope)))))
        ;; (define name [doc] expr) -- strings expand to themselves.
        (cons (car form)
              (cons target
                    (exp-list (cdr (cdr form)) (cons target scope)))))))

(define (exp-defmacro form scope)
  (let ((name (car (cdr form)))
        (params (car (cdr (cdr form)))))
    (cons 'defmacro
          (cons name
                (cons params
                      (exp-body (cdr (cdr (cdr form)))
                                (append (exp-param-names params) scope)))))))

(define (exp-set! form scope)
  (list 'set! (car (cdr form)) (exp-form (car (cdr (cdr form))) scope)))

;; cond: clause structure kept, tests and bodies expanded ('else is a symbol
;; and expands to itself).
(define (exp-cond-clauses cs scope)
  (if (pair? cs)
      (cons (exp-list (car cs) scope) (exp-cond-clauses (cdr cs) scope))
      '()))

;; try: body forms plus trailing (catch (pred var) forms...) clauses.
(define (exp-try-forms forms scope)
  (if (pair? forms)
      (let ((f (car forms)))
        (cons
         (if (and (pair? f) (eq? (car f) 'catch))
             (let ((spec (car (cdr f))))
               (let ((pred (car spec)) (var (car (cdr spec))))
                 (cons 'catch
                       (cons (list (exp-form pred scope) var)
                             (exp-body (cdr (cdr f)) (cons var scope))))))
             (exp-form f scope))
         (exp-try-forms (cdr forms) scope)))
      '()))

;; restart-case clauses: (name ["desc"] (params) body...)
(define (exp-restart-clauses cs scope)
  (if (pair? cs)
      (let ((c (car cs)))
        (let ((name (car c)) (r (cdr c)))
          (let ((has-desc (and (string? (car r)) (pair? (cdr r)))))
            (let ((desc (if has-desc (car r) nil))
                  (r2 (if has-desc (cdr r) r)))
              (let ((params (car r2)))
                (let ((body (exp-body (cdr r2)
                                      (append (exp-param-names params) scope))))
                  (cons
                   (if has-desc
                       (cons name (cons desc (cons params body)))
                       (cons name (cons params body)))
                   (exp-restart-clauses (cdr cs) scope))))))))
      '()))

;; quasiquote with depth accounting: only depth-1 unquotes are expanded.
(define (exp-qq x depth scope)
  (if (pair? x)
      (let ((h (car x)))
        (cond ((eq? h 'quasiquote)
               (list 'quasiquote (exp-qq (car (cdr x)) (+ depth 1) scope)))
              ((or (eq? h 'unquote) (eq? h 'unquote-splicing))
               (if (= depth 1)
                   (list h (exp-form (car (cdr x)) scope))
                   (list h (exp-qq (car (cdr x)) (- depth 1) scope))))
              (else (exp-qq-list x depth scope))))
      x))

(define (exp-qq-list x depth scope)
  (cond ((pair? x)
         (if (or (eq? (car x) 'unquote) (eq? (car x) 'unquote-splicing))
             (exp-qq x depth scope)                 ; dotted (a . ,b) tail
             (cons (exp-qq (car x) depth scope)
                   (exp-qq-list (cdr x) depth scope))))
        (else x)))

;; --- macro invocation -------------------------------------------------------

;; The expander invokes macro objects through apply's privileged macro path.
(define (exp-call-macro m args) (apply m args))

;; --- the expander proper ----------------------------------------------------

(define (exp-form form scope)
  (if (pair? form)
      (let ((head (car form)))
        (if (symbol? head)
            (cond ((eq? head 'quote) form)
                  ((eq? head 'quasiquote)
                   (list 'quasiquote (exp-qq (car (cdr form)) 1 scope)))
                  ((or (eq? head 'lambda) (eq? head 'fn)) (exp-lambda form scope))
                  ((exp-define-head? head) (exp-define form scope))
                  ((eq? head 'defmacro) (exp-defmacro form scope))
                  ((eq? head 'set!) (exp-set! form scope))
                  ((eq? head 'let) (exp-let form scope))
                  ((or (eq? head 'begin) (eq? head 'do)
                       (eq? head 'if) (eq? head 'while)
                       (eq? head 'and) (eq? head 'or)
                       (eq? head 'unwind-protect) (eq? head 'defer))
                   (cons head (exp-body (cdr form) scope)))
                  ((eq? head 'cond)
                   (cons 'cond (exp-cond-clauses (cdr form) scope)))
                  ((or (eq? head 'ns) (eq? head 'in-ns) (eq? head 'require))
                   form)                             ; specs are data
                  ((eq? head 'defparam)
                   (cons 'defparam
                         (cons (car (cdr form))
                               (exp-list (cdr (cdr form)) scope))))
                  ((or (eq? head 'handler-bind) (eq? head 'with-params))
                   (cons head
                         (cons (exp-pair-list (car (cdr form)) scope)
                               (exp-body (cdr (cdr form)) scope))))
                  ((eq? head 'restart-case)
                   (cons 'restart-case
                         (cons (exp-form (car (cdr form)) scope)
                               (exp-restart-clauses (cdr (cdr form)) scope))))
                  ((eq? head 'try)
                   (cons 'try (exp-try-forms (cdr form) scope)))
                  ((or (eq? head 'unquote) (eq? head 'unquote-splicing))
                   form)                             ; error at eval time
                  ((exp-memq head scope)
                   (exp-list form scope))            ; lexical shadow: plain call
                  (else
                   (let ((m (expander-macro-var head)))
                     (if m
                         (exp-form (exp-call-macro m (cdr form)) scope)
                         (exp-list form scope)))))
            (exp-list form scope)))
      form))

(define (expand form) (exp-form form '()))

;; The evaluator calls (*expander* form) for each top-level form and from
;; eval/macroexpand. Macro lookup is provided by expander-macro-var.
(define *expander* expand)
