(library (otium values)
  (export keyword keyword? keyword->string nil? array array? table table?
          collection-call buffer buffer? buffer-push! buffer->string str
          get put! push! pop! keys table-values copy collection-length collection-reverse
          collection-for-each collection-apply update!
          otium-write otium-display type frozen-set-car! frozen-set-cdr!)
  (import (chezscheme))

  (define-record-type (keyword-value make-keyword keyword?) (fields name))
  (define keywords (make-hashtable string-hash string=?))
  (define (keyword name)
    (let ([name (cond [(keyword? name) (keyword->string name)]
                      [(symbol? name) (symbol->string name)] [else name])])
      (unless (string? name) (error 'keyword "expected string or symbol" name))
      (or (hashtable-ref keywords name #f)
          (let ([value (make-keyword (string->immutable-string name))])
            (hashtable-set! keywords (keyword-value-name value) value) value))))
  (define keyword->string keyword-value-name)
  (define (nil? x) (eq? x (void)))
  (define-record-type (array-value make-array array?)
    (fields (mutable storage) (mutable size)))
  (define (array . xs) (make-array (list->vector xs) (length xs)))
  (define (push! a . xs)
    (unless (array? a) (error 'push! "expected array" a))
    (let* ([old (array-value-storage a)] [size (array-value-size a)]
           [needed (+ size (length xs))])
      (when (> needed (vector-length old))
        (let ([new (make-vector (max needed 4 (* 2 (vector-length old))) (void))])
          (do ([i 0 (+ i 1)]) ((= i size)) (vector-set! new i (vector-ref old i)))
          (array-value-storage-set! a new)))
      (let loop ([xs xs] [i size])
        (unless (null? xs)
          (vector-set! (array-value-storage a) i (car xs))
          (loop (cdr xs) (+ i 1))))
      (array-value-size-set! a needed)) a)
  (define (pop! a)
    (unless (array? a) (error 'pop! "expected array" a))
    (let ([n (array-value-size a)])
      (if (= n 0) (void)
          (let ([x (vector-ref (array-value-storage a) (- n 1))])
            (vector-set! (array-value-storage a) (- n 1) (void))
            (array-value-size-set! a (- n 1)) x))))

  (define-record-type (buffer-value make-buffer buffer?)
    (fields port (mutable size) (mutable snapshot)))
  (define (buffer . seed)
    (unless (<= (length seed) 1) (error 'buffer "expected optional seed"))
    (let ([b (make-buffer (open-output-string) 0 "")])
      (apply buffer-push! b seed) b))
  (define (str . xs)
    (let ([port (open-output-string)])
      (for-each (lambda (x) (otium-display x port)) xs)
      (get-output-string port)))
  (define (buffer-push! b . xs)
    (unless (buffer? b) (error 'buffer-push! "expected buffer"))
    (for-each (lambda (x)
                (let ([s (str x)])
                  (display s (buffer-value-port b))
                  (buffer-value-size-set! b (+ (buffer-value-size b) (string-length s))))) xs)
    b)
  (define (buffer->string b)
    (unless (buffer? b) (error 'buffer->string "expected buffer"))
    (let ([s (string-append (buffer-value-snapshot b) (get-output-string (buffer-value-port b)))])
      (buffer-value-snapshot-set! b s) s))
  (define (collection-call f . args)
    (cond [(procedure? f) (apply f args)]
          [(or (table? f) (array? f))
           (unless (<= 1 (length args) 2) (error 'call "expected key and optional default"))
           (when (and (array? f) (not (and (integer? (car args)) (exact? (car args)))))
             (error 'call "expected integer index"))
           (apply get f args)]
          [(keyword? f)
           (unless (<= 1 (length args) 2)
             (error 'call "expected table or nil and optional default"))
           (if (table? (car args)) (apply get (car args) f (cdr args))
               (if (null? (cdr args)) (void) (cadr args)))]
          [else (error 'call "expected callable value" f)]))

  (define frozen (make-weak-eq-hashtable))
  (define (key=? a b)
    (or (eq? a b)
        (cond [(and (string? a) (string? b)) (string=? a b)]
              [(and (number? a) (number? b))
               (and (eq? (exact? a) (exact? b))
                    (or (= a b) (and (real? a) (real? b) (nan? a) (nan? b))))]
              [(and (pair? a) (pair? b)) (and (key=? (car a) (car b)) (key=? (cdr a) (cdr b)))]
              [else #f])))
  (define (key-hash x)
    (cond [(pair? x)
           (hashtable-set! frozen x #t)
           (bitwise-and #x3fffffff (+ (* 33 (key-hash (car x))) (key-hash (cdr x))))]
          [(string? x) (string-hash x)]
          [(number? x) (if (and (real? x) (nan? x)) 17 (equal-hash (if (= x 0) 0 x)))]
          [else (equal-hash x)]))
  (define-record-type (table-value make-table table?) (fields data (mutable order)))
  (define (table . xs)
    (unless (even? (length xs)) (error 'table "expected key/value pairs"))
    (let ([t (make-table (make-hashtable key-hash key=?) '())])
      (unless (null? xs) (apply put! t xs)) t))
  (define (get x key . default)
    (unless (<= (length default) 1) (error 'get "expected 2 or 3 arguments"))
    (let ([fallback (if (null? default) (void) (car default))])
      (cond [(nil? x) fallback]
            [(table? x) (hashtable-ref (table-value-data x) key fallback)]
            [(or (array? x) (string? x))
             (if (and (integer? key) (exact? key) (<= 0 key) (< key (collection-length x)))
                 (if (array? x) (vector-ref (array-value-storage x) key) (string (string-ref x key)))
                 fallback)]
            [else (error 'get "expected nil, table, array, or string" x)])))
  (define (put! x . xs)
    (unless (and (pair? xs) (even? (length xs))) (error 'put! "expected key/value pairs"))
    (unless (or (array? x) (table? x)) (error 'put! "expected array or table" x))
    (let loop ([xs xs])
      (unless (null? xs)
        (let ([key (car xs)] [value (cadr xs)])
          (if (array? x)
              (begin
                (unless (and (integer? key) (exact? key) (<= 0 key) (< key (array-value-size x)))
                  (error 'put! "index out of range" key))
                (vector-set! (array-value-storage x) key value))
              (let ([data (table-value-data x)])
                (cond [(nil? value)
                       (hashtable-delete! data key)
                       (table-value-order-set! x (remp (lambda (k) (key=? k key)) (table-value-order x)))]
                      [else
                       (unless (hashtable-contains? data key)
                         (table-value-order-set! x (cons key (table-value-order x))))
                       (hashtable-set! data key value)])))
          (loop (cddr xs))))) x)
  (define (keys x)
    (cond [(nil? x) (array)] [(table? x) (apply array (reverse (table-value-order x)))]
          [else (error 'keys "expected table" x)]))
  (define (table-values x)
    (cond [(nil? x) (array)]
          [(table? x) (apply array (map (lambda (k) (get x k)) (reverse (table-value-order x))))]
          [else (error 'values "expected table" x)]))
  (define (array-list x)
    (let loop ([i (- (array-value-size x) 1)] [out '()])
      (if (< i 0) out (loop (- i 1) (cons (get x i) out)))))
  (define (collection-length x)
    (cond [(nil? x) 0] [(array? x) (array-value-size x)]
          [(table? x) (hashtable-size (table-value-data x))]
          [(buffer? x) (buffer-value-size x)]
          [(string? x) (string-length x)] [else (length x)]))
  (define (copy x)
    (cond [(nil? x) x] [(array? x) (apply array (array-list x))]
          [(table? x) (let ([t (table)]) (for-each (lambda (k) (put! t k (get x k))) (reverse (table-value-order x))) t)]
          [else (error 'copy "expected array, table, or nil" x)]))
  (define (collection-reverse x)
    (cond [(array? x) (apply array (reverse (array-list x)))] [(nil? x) '()] [else (reverse x)]))
  (define (sequence-list x) (cond [(array? x) (array-list x)] [(nil? x) '()] [else x]))
  (define (collection-for-each f x . xs)
    (if (and (array? x) (null? xs))
        (let ([n (array-value-size x)])
          (do ([i 0 (+ i 1)]) ((= i n)) (collection-call f (get x i))))
        (apply for-each (lambda args (apply collection-call f args)) (map sequence-list (cons x xs)))))
  (define (collection-apply f . args)
    (when (null? args) (error 'apply "expected argument sequence"))
    (let ([rev (reverse args)])
      (apply apply collection-call f (reverse (cons (sequence-list (car rev)) (cdr rev))))))
  (define (update! x key f . args) (put! x key (apply f (get x key) args)))
  (define (reaches? target value)
    (let ([seen (make-eq-hashtable)])
      (let walk ([x value])
        (cond [(eq? x target) #t] [(hashtable-ref seen x #f) #f]
              [else (hashtable-set! seen x #t)
                    (cond [(pair? x) (or (walk (car x)) (walk (cdr x)))]
                          [(array? x) (exists walk (array-list x))]
                          [(table? x) (exists (lambda (k) (or (walk k) (walk (get x k)))) (table-value-order x))]
                          [else #f])]))))
  (define (mutate-pair! setter x value)
    (unless (pair? x) (error 'set-pair! "expected pair" x))
    (when (hashtable-ref frozen x #f) (error 'set-pair! "frozen table key"))
    (when (reaches? x value) (error 'set-pair! "cycle"))
    (setter x value) x)
  (define (frozen-set-car! x v) (mutate-pair! set-car! x v))
  (define (frozen-set-cdr! x v) (mutate-pair! set-cdr! x v))
  (define (type x)
    (keyword (cond [(buffer? x) "buffer"] [(nil? x) "nil"] [(keyword? x) "keyword"] [(array? x) "array"] [(table? x) "table"]
          [(null? x) "null"] [(pair? x) "pair"] [(boolean? x) "boolean"] [(symbol? x) "symbol"]
          [(string? x) "string"] [(and (integer? x) (exact? x)) "int"]
          [(number? x) "float"] [(procedure? x) "function"] [else "native"])))
  (define (render x port display? depth)
    (define (items xs)
      (unless (null? xs)
        (render (car xs) port #f (+ depth 1))
        (unless (null? (cdr xs)) (display " " port) (items (cdr xs)))))
    (cond [(> depth 100) (display "..." port)]
          [(buffer? x)
           (if display? (display (buffer->string x) port)
               (begin (display "#<buffer " port) (write (buffer->string x) port) (display ">" port)))]
          [(nil? x) (display "nil" port)]
          [(keyword? x) (display ":" port) (display (keyword->string x) port)]
          [(array? x) (display "[" port) (items (array-list x)) (display "]" port)]
          [(table? x) (display "{" port)
           (items (apply append (map (lambda (k) (list k (get x k))) (reverse (table-value-order x)))))
           (display "}" port)]
          [(pair? x)
           (display "(" port)
           (let loop ([x x] [n depth])
             (render (car x) port #f (+ n 1))
             (cond [(null? (cdr x)) (display ")" port)]
                   [(and (pair? (cdr x)) (< n 100)) (display " " port) (loop (cdr x) (+ n 1))]
                   [else (display " . " port) (render (cdr x) port #f (+ n 1)) (display ")" port)]))]
          [else ((if display? display write) x port)]))
  (define (otium-write x . port) (render x (if (null? port) (current-output-port) (car port)) #f 0))
  (define (otium-display x . port) (render x (if (null? port) (current-output-port) (car port)) #t 0)))
