(library (otium core)
;; This export list is the default vocabulary, independent of Chez additions.
  ;; Names here should exist on C too; Chez representations (vectors,
  ;; hashtables, bytevectors, chars, continuations) are reached through
  ;; (require chez). The Scheme list and comparison helpers below that C
  ;; lacks are pending a naming decision.
  (export
    quote quasiquote unquote unquote-splicing if begin set! and or cond else
    when unless
    (rename (ot-fn fn) (ot-def def) (ot-def def-) (ot-let let) (ot-while while)
            (ot-defparam defparam) (ot-with-params with-params))
    + - * / = < > <= >= zero? positive? negative? odd? even?
    min max abs quotient remainder modulo gcd lcm
    floor ceiling truncate round exp log sin cos tan asin acos atan sqrt expt
    number? integer? exact? inexact? exact inexact number->string string->number
    not boolean? eq? eqv? equal? procedure?
    cons car cdr caar cadr cdar cddr caaar caadr cadar caddr
    cdaar cdadr cddar cdddr cadddr pair? null? list? list length append reverse
    list-tail list-ref memq memv member assq assv assoc set-car! set-cdr!
    map for-each apply filter fold-left fold-right for-all exists
    symbol? symbol->string string->symbol gensym
    string? string-length string=? string<? string>? string<=? string>=?
    substring string-append
    values
    display print println newline
    read-string keyword keyword? keyword->string nil? array array? table table?
    get put! push! pop! keys copy take drop update! type)
  (import (except (chezscheme) display newline if and or cond when unless do not filter exists for-all
                          length reverse for-each map apply set-car! set-cdr! values)
          (otium truth) (otium reader)
          (rename (otium values) (collection-length length) (collection-reverse reverse)
                  (collection-for-each for-each) (collection-map map) (collection-apply apply)
                  (frozen-set-car! set-car!) (frozen-set-cdr! set-cdr!))
          (prefix (only (chezscheme) values) host:)
          (prefix (only (chezscheme) display newline) host:))

  ;; Otium core forms over Chez. The shared expander has already normalized
  ;; parameter lists and moved body definitions ahead of plain expressions.
  (define-syntax ot-fn
    (syntax-rules () [(_ params body ...) (lambda params body ...)]))

  (define-syntax ot-def
    (lambda (x)
      (syntax-case x ()
        [(_ (name . params) doc first body ...) (string? (syntax->datum #'doc))
         #'(define (name . params) first body ...)]
        [(_ (name . params) body ...) #'(define (name . params) body ...)]
        [(_ name doc value) (string? (syntax->datum #'doc)) #'(define name value)]
        [(_ name value) #'(define name value)])))

  ;; Otium let binds sequentially; named let initializers see the outer scope.
  (define-syntax ot-let
    (syntax-rules ()
      [(_ (bindings ...) body ...) (let* (bindings ...) body ...)]
      [(_ name (bindings ...) body ...) (let name (bindings ...) body ...)]))

  (define-syntax ot-while
    (syntax-rules ()
      [(_ test body ...) (let loop () (if test (begin body ... (loop)) (void)))]))

  (define-syntax ot-defparam
    (lambda (x)
      (syntax-case x ()
        [(_ name doc value) (string? (syntax->datum #'doc))
         #'(define name (make-parameter value))]
        [(_ name value) #'(define name (make-parameter value))])))

  ;; Bindings are installed one at a time, as on C.
  (define-syntax ot-with-params
    (syntax-rules ()
      [(_ () body ...) (begin (void) body ...)]
      [(_ ((param value) more ...) body ...)
       (parameterize ([param value]) (ot-with-params (more ...) body ...))]))

  (define (display . objects)
    (let loop ([objects objects] [first? #t])
      (unless (null? objects)
        (unless first? (host:display " "))
        (otium-display (car objects))
        (loop (cdr objects) #f)))
    #f)

  (define (values . xs)
    (if (and (= (length xs) 1) (or (table? (car xs)) (nil? (car xs))))
        (table-values (car xs)) (apply host:values xs)))

  (define print display)

  (define (newline)
    (host:newline)
    #f)

  (define (println . objects)
    (apply display objects)
    (newline)))
