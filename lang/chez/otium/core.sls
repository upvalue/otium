(library (otium core)
;; This export list is the default vocabulary, independent of Chez additions.
  ;; Names here should exist on C too; Chez representations (vectors,
  ;; hashtables, continuations) are reached through
  ;; (require chez).
  (export
    quote quasiquote unquote unquote-splicing if begin set! and or cond else
    when unless
    (rename (ot-fn fn) (ot-def def) (ot-def def-) (ot-let let) (ot-while while)
            (ot-defparam defparam) (ot-with-params with-params))
    + - * (rename (ot-divide /)) = < > <= >= zero? positive? negative? odd? even?
    min max abs quotient remainder modulo gcd lcm
    bitwise-and bitwise-ior bitwise-xor bitwise-not bitwise-arithmetic-shift
    (rename (ot-floor floor) (ot-ceiling ceiling) (ot-truncate truncate)
            (ot-round round) (ot-sqrt sqrt))
    exp log sin cos tan asin acos atan expt
    number? integer? exact? inexact? finite?
    exact inexact number->string
    not boolean? eq? (rename (ot-equal? equal?)) procedure?
    cons car cdr caar cadr cdar cddr caaar caadr cadar caddr
    cdaar cdadr cddar cdddr cadddr pair? null? list? list length reverse
    list-tail list-ref memq member assq assoc set-car! set-cdr!
    for-each apply
    symbol? symbol->string string->symbol gensym
    string? string-length string=? string<? string>? string<=? string>=? string-append
    string string-ref string->list list->string
    char? char->integer integer->char char-upcase char-downcase
    char-alphabetic? char-numeric? char-whitespace? eof-object eof-object?
    bytevector? bytevector make-bytevector bytevector-length bytevector-u8-ref
    bytevector-u8-set! bytevector-copy bytevector-copy! bytevector-append
    utf8->string string->utf8
    values
    display print println newline write
    (rename (ot-append append) (ot-call otium-call) (ot-unary-call otium-unary-call) (ot-substring substring) (ot-string->number string->number))
    str buffer buffer? buffer-push! buffer->string
    string-split string-join string-contains? string-starts-with? string-ends-with?
    string-replace string-trim string-upcase string-downcase
    name symbol int? float? inc dec identity pos? neg? true? false?
    current-jiffy jiffies-per-second current-second read-file
    read-string keyword keyword? keyword->string nil? array array? table table?
    %make-record-type record-new record-ref record-set! record? record-type? record-type
    get put! set-applied! push! pop! keys copy update! type)
  (import (except (chezscheme) record? display newline write if and or cond when unless do not filter exists for-all
                          length reverse for-each map apply set-car! set-cdr! values
                          bytevector make-bytevector bytevector-copy bytevector-copy!
                          utf8->string string->utf8)
          (otium bytevectors)
          (otium truth) (otium reader)
          (rename (otium values) (collection-length length) (collection-reverse reverse)
                  (collection-for-each for-each) (collection-apply apply)
                  (frozen-set-car! set-car!) (frozen-set-cdr! set-cdr!))
          (prefix (only (chezscheme) display newline write for-each apply) host:))

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

  ;; The expander passes the procedure call itself as the last operand, built
  ;; from names it chose, so that the call keeps the source position of the
  ;; Otium application and errors raised inside it point there.
  (define-syntax ot-call
    (lambda (x)
      (syntax-case x ()
        [(_ callee call)
         (syntax-case #'call ()
           [(f arg ...)
            #'(let ([f callee])
                (if (procedure? f) call (collection-call f arg ...)))])])))
  ;; Keep procedure calls visible to Chez without going through the variadic
  ;; collection dispatcher. Preserve unary calls' argument-before-callee order.
  (define-syntax ot-unary-call
    (lambda (x)
      (syntax-case x ()
        [(_ fallback cache callee arg call)
         (syntax-case #'call ()
           [(f a)
            #'(let* ([a arg] [f callee])
                (if (procedure? f) call (fallback cache f a)))])])))
  ;; All arguments must be proper lists, including the final one. Common
  ;; arities avoid packing arguments; the fallback uses native list helpers.
  (define ot-append
    (case-lambda
      [() '()]
      [(xs) (unless (list? xs) (error 'append "expected proper list" xs)) xs]
      [(xs ys)
       (unless (list? xs) (error 'append "expected proper list" xs))
       (unless (list? ys) (error 'append "expected proper list" ys))
       (append xs ys)]
      [lists
       (host:for-each (lambda (xs)
                       (unless (list? xs) (error 'append "expected proper list" xs)))
                     lists)
       (host:apply append lists)]))
  ;; Bytevectors compare by identity in Otium, unlike Chez's equal?.
  (define (ot-equal? a b)
    (cond [(eq? a b) #t]
          [(pair? a) (and (pair? b) (ot-equal? (car a) (car b)) (ot-equal? (cdr a) (cdr b)))]
          [(or (bytevector? a) (bytevector? b)) #f]
          [else (equal? a b)]))
  (define (int? x) (and (integer? x) (exact? x)))
  (define (float? x) (and (real? x) (inexact? x)))
  (define (ot-number who x)
    (unless (or (int? x) (flonum? x)) (error who "expected int or float" x)))

  (define (ot-divide first . rest)
    (define (divide a b)
      (ot-number '/ b)
      (when (zero? b) (error '/ "division by zero"))
      (if (and (int? a) (int? b) (zero? (remainder a b)))
          (quotient a b)
          (fl/ (inexact a) (inexact b))))
    (ot-number '/ first)
    (if (null? rest)
        (divide 1.0 first)
        (let loop ([result first] [rest rest])
          (if (null? rest) result
              (loop (divide result (car rest)) (cdr rest))))))

  ;; Match the C tagged integer range for float conversions. Exact integers
  ;; retain Chez's unbounded representation.
  (define int-limit (expt 2 (- (* 8 (foreign-sizeof 'void*)) 2)))
  (define (ot-rounding who operation x)
    (ot-number who x)
    (if (int? x) x
        (let ([result (operation x)])
          (unless (and (finite? result) (<= (- int-limit) result) (< result int-limit))
            (error who "integer conversion out of range" x))
          (exact result))))
  (define (ot-floor x) (ot-rounding 'floor floor x))
  (define (ot-ceiling x) (ot-rounding 'ceiling ceiling x))
  (define (ot-truncate x) (ot-rounding 'truncate truncate x))
  (define (ot-round x)
    (ot-rounding 'round
      (lambda (x) (if (< x 0) (ceiling (- x 0.5)) (floor (+ x 0.5)))) x))
  (define (ot-sqrt x)
    (ot-number 'sqrt x)
    (if (< x 0) +nan.0 (flsqrt (inexact x))))

  (define (inc x) (+ x 1))
  (define (dec x) (- x 1))
  (define (identity x) x)
  (define pos? positive?)
  (define neg? negative?)
  (define (true? x) (eq? x #t))
  (define (false? x) (eq? x #f))
  (define (name x)
    (cond [(string? x) x] [(symbol? x) (symbol->string x)]
          [(keyword? x) (keyword->string x)] [else (error 'name "expected string, symbol, or keyword")]))
  (define (symbol x) (string->symbol (name x)))
  (define (ot-substring s start . end)
    (unless (and (string? s) (int? start) (<= (length end) 1)
                 (or (null? end) (int? (car end))))
      (error 'substring "expected string, integer start and optional end"))
    (let* ([n (string-length s)] [a (max 0 (min n start))]
           [b (max a (min n (if (null? end) n (car end))))])
      (substring s a b)))
  (define (ascii-space? c) (memv c '(#\space #\tab #\return #\newline #\page #\vtab)))
  (define (string-trim s)
    (let ([n (string-length s)])
      (let left ([a 0])
        (if (and (< a n) (ascii-space? (string-ref s a))) (left (+ a 1))
            (let right ([b n])
              (if (and (> b a) (ascii-space? (string-ref s (- b 1)))) (right (- b 1))
                  (substring s a b)))))))
  (define (ot-string->number s) (or (string->number (string-trim s)) (void)))
  (define (find-text s part start)
    (let ([n (string-length s)] [m (string-length part)])
      (let loop ([i start])
        (cond [(> (+ i m) n) #f]
              [(string=? (substring s i (+ i m)) part) i]
              [else (loop (+ i 1))]))))
  (define (string-contains? s part) (if (find-text s part 0) #t #f))
  (define (string-starts-with? s part)
    (and (<= (string-length part) (string-length s))
         (string=? part (substring s 0 (string-length part)))))
  (define (string-ends-with? s part)
    (and (<= (string-length part) (string-length s))
         (string=? part (substring s (- (string-length s) (string-length part)) (string-length s)))))
  (define (string-split s . separator)
    (unless (<= (length separator) 1) (error 'string-split "expected optional separator"))
    (let ([out (array)] [n (string-length s)])
      (if (null? separator)
          (let loop ([i 0])
            (when (< i n)
              (if (ascii-space? (string-ref s i)) (loop (+ i 1))
                  (let end ([j i])
                    (if (and (< j n) (not (ascii-space? (string-ref s j)))) (end (+ j 1))
                        (begin (push! out (substring s i j)) (loop j)))))))
          (let ([sep (car separator)])
            (when (= 0 (string-length sep)) (error 'string-split "empty separator"))
            (let loop ([i 0])
              (let ([j (find-text s sep i)])
                (push! out (substring s i (or j n)))
                (when j (loop (+ j (string-length sep))))))))
      out))
  (define (string-join separator xs)
    (let ([out (buffer)] [first? #t])
      (for-each (lambda (x)
                  (unless first? (buffer-push! out separator))
                  (set! first? #f) (buffer-push! out x)) xs)
      (buffer->string out)))
  (define (string-replace s from to)
    (if (= 0 (string-length from)) s (string-join to (string-split s from))))
  (define (current-jiffy)
    (let ([t (current-time 'time-monotonic)])
      (+ (* (time-second t) 1000000000) (time-nanosecond t))))
  (define (jiffies-per-second) 1000000000)
  (define (current-second)
    (let ([t (current-time 'time-utc)])
      (+ (inexact (time-second t)) (/ (time-nanosecond t) 1000000000.0))))
  (define (read-file path)
    (call-with-port (open-file-input-port path (file-options) 'block
                     (make-transcoder (utf-8-codec) 'none 'raise))
      (lambda (port) (let ([s (get-string-all port)]) (if (eof-object? s) "" s)))))
  (define (write . objects)
    (let loop ([xs objects])
      (unless (null? xs)
        (otium-write (car xs))
        (unless (null? (cdr xs)) (host:display " "))
        (loop (cdr xs))))
    (void))

  (define (display . objects)
    (let loop ([objects objects] [first? #t])
      (unless (null? objects)
        (unless first? (host:display " "))
        (otium-display (car objects))
        (loop (cdr objects) #f)))
    (void))

  (define values table-values)

  (define print display)

  (define (newline)
    (host:newline)
    (void))

  (define (println . objects)
    (apply display objects)
    (newline)))
