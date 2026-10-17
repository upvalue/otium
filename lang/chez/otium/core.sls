(library (otium core)
  ;; This export list is the default vocabulary, independent of Chez additions.
  (export
    quote quasiquote unquote unquote-splicing if begin lambda define set!
    let let* letrec letrec* and or cond else => case do when unless
    define-syntax syntax-rules let-syntax letrec-syntax
    let-values let*-values case-lambda
    + - * / = < > <= >= zero? positive? negative? odd? even?
    min max abs quotient remainder modulo gcd lcm numerator denominator
    floor ceiling truncate round exp log sin cos tan asin acos atan sqrt expt
    number? complex? real? rational? integer? exact? inexact?
    exact inexact exact->inexact inexact->exact number->string string->number
    not boolean? eq? eqv? equal? procedure?
    cons car cdr caar cadr cdar cddr caaar caadr cadar caddr
    cdaar cdadr cddar cdddr cadddr pair? null? list? list length append reverse
    list-tail list-ref memq memv member assq assv assoc set-car! set-cdr!
    map for-each apply filter fold-left fold-right for-all exists
    symbol? symbol->string string->symbol gensym
    char? char=? char<? char>? char<=? char>=? char->integer integer->char
    char-alphabetic? char-numeric? char-whitespace? char-upper-case?
    char-lower-case? char-upcase char-downcase
    string? string make-string string-length string-ref string-set!
    string=? string<? string>? string<=? string>=? substring string-append
    string->list list->string string-copy string-fill!
    vector? vector make-vector vector-length vector-ref vector-set!
    vector->list list->vector vector-fill!
    bytevector? make-bytevector bytevector-length bytevector-u8-ref bytevector-u8-set!
    make-eq-hashtable make-eqv-hashtable make-hashtable hashtable?
    hashtable-size hashtable-ref hashtable-set! hashtable-delete!
    hashtable-contains? hashtable-update! hashtable-copy hashtable-clear!
    hashtable-keys hashtable-entries equal-hash string-hash symbol-hash
    values call-with-values call/cc call-with-current-continuation dynamic-wind
    make-parameter parameterize void
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
