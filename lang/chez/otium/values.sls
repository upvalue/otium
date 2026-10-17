(library (otium values)
  (export keyword keyword? keyword->string nil? array array? table table?
          get put! set-applied! push! pop! keys table-values copy collection-length collection-reverse
          collection-for-each collection-map collection-apply take drop update!
          otium-write otium-display type frozen-set-car! frozen-set-cdr!
          make-instance instance? instance-type instance-name instance-layout instance-layout-set!
          instance-slots instance-slots-set! instance-migrating? instance-migrating?-set!
          install-instance-operations! callable-apply)
  (import (chezscheme))

  (define-record-type (instance make-instance instance?)
    (opaque #t)
    (fields type name (mutable layout) (mutable slots) (mutable migrating?)))
  (define instance-read #f)
  (define instance-write #f)
  (define instance-copy #f)
  (define (install-instance-operations! read write copy)
    (set! instance-read read) (set! instance-write write) (set! instance-copy copy))
  (define (callable-apply target . args)
    (cond [(instance? target)
           (unless (= (length args) 1) (error 'slot-ref "expected one keyword"))
           (instance-read target (car args))]
          [(or (array? target) (table? target))
           (unless (= (length args) 1) (error 'collection-call "expected one key"))
           (get target (car args))]
          [else (apply target args)]))

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
  (define (set-applied! object key value)
    (cond [(instance? object) (instance-write object key value)]
          [(or (table? object) (array? object)) (put! object key value) value]
          [else (error 'set-applied! "cannot assign through type" (type object))]))
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
          [(string? x) (string-length x)] [else (length x)]))
  (define (copy x)
    (cond [(instance? x) (instance-copy x)] [(nil? x) x] [(array? x) (apply array (array-list x))]
          [(table? x) (let ([t (table)]) (for-each (lambda (k) (put! t k (get x k))) (reverse (table-value-order x))) t)]
          [else (error 'copy "expected array, table, or nil" x)]))
  (define (collection-reverse x)
    (cond [(array? x) (apply array (reverse (array-list x)))] [(nil? x) '()] [else (reverse x)]))
  (define (sequence-list x) (cond [(array? x) (array-list x)] [(nil? x) '()] [else x]))
  (define (collection-for-each f x . xs)
    (if (and (array? x) (null? xs))
        (let ([n (array-value-size x)])
          (do ([i 0 (+ i 1)]) ((= i n)) (f (get x i))))
        (apply for-each f (map sequence-list (cons x xs)))))
  (define (collection-map f x . xs)
    (let ([out (apply map f (map sequence-list (cons x xs)))])
      (if (array? x) (apply array out) out)))
  (define (collection-apply f . args)
    (when (null? args) (error 'apply "expected argument sequence"))
    (let ([rev (reverse args)])
      (apply apply callable-apply f (reverse (cons (sequence-list (car rev)) (cdr rev))))))
  (define (take n x)
    (unless (and (integer? n) (exact? n)) (error 'take "expected integer count" n))
    (let loop ([n n] [xs (sequence-list x)] [out '()])
      (if (or (<= n 0) (null? xs)) (if (array? x) (apply array (reverse out)) (reverse out))
          (loop (- n 1) (cdr xs) (cons (car xs) out)))))
  (define (drop n x)
    (unless (and (integer? n) (exact? n)) (error 'drop "expected integer count" n))
    (let loop ([n n] [xs (sequence-list x)])
      (if (or (<= n 0) (null? xs)) (if (array? x) (apply array xs) xs)
          (loop (- n 1) (cdr xs)))))
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
    (keyword (cond [(instance? x) "instance"] [(nil? x) "nil"] [(keyword? x) "keyword"] [(array? x) "array"] [(table? x) "table"]
          [(null? x) "null"] [(pair? x) "pair"] [(boolean? x) "boolean"] [(symbol? x) "symbol"]
          [(string? x) "string"] [(and (integer? x) (exact? x)) "int"]
          [(number? x) "float"] [(procedure? x) "function"] [else "native"])))
  (define (render x port display? depth)
    (define (items xs)
      (unless (null? xs)
        (render (car xs) port #f (+ depth 1))
        (unless (null? (cdr xs)) (display " " port) (items (cdr xs)))))
    (cond [(instance? x)
           (display "#<" port) (display (instance-name x) port) (display ">" port)]
          [(> depth 100) (display "..." port)]
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
