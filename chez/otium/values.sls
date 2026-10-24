(library (otium values)
  (export %make-record-type record-new record-ref record-set! record? record-type? record-type
          keyword keyword? keyword->string nil? array array? table table?
          collection-call buffer buffer? buffer-push! buffer->string str
          get put! set-applied! push! pop! keys table-values copy collection-length collection-reverse
          collection-for-each collection-apply update!
          otium-write otium-display type frozen-set-car! frozen-set-cdr!
          make-instance instance? instance-type instance-name instance-layout instance-layout-set!
          instance-slots instance-slots-set! instance-migrating? instance-migrating?-set!
          install-instance-operations!
          make-port-value port-value? port-value-name port-value-input? port-value-kind
          port-value-host port-value-buffer port-value-at port-value-at-set!
          port-value-end port-value-end-set! port-value-position port-value-position-set!
          port-value-closed? port-value-closed?-set! port-value-eof? port-value-eof?-set!
          port-value-callbacks port-value-extract)
  (import (except (chezscheme) record?))

  (define-record-type record-descriptor
    (opaque #t)
    (fields name keys rtd constructor readers writers))
  (define record-type? record-descriptor?)
  (define-record-type record-base (opaque #t) (fields descriptor))
  (define record? record-base?)
  (define (record-type object)
    (unless (record? object) (error 'record-type "expected record" object))
    (record-base-descriptor object))
  (define (%make-record-type name fields immutable?)
    (unless (and (symbol? name) (list? fields) (for-all symbol? fields) (boolean? immutable?))
      (error 'defrecord "expected name, field list and boolean immutability"))
    (let* ([keys (map keyword fields)]
           [rtd (make-record-type-descriptor name (record-type-descriptor record-base)
                  #f #t #t
                  (list->vector (map (lambda (field) (list (if immutable? 'immutable 'mutable) field)) fields)))]
           [rcd (make-record-constructor-descriptor rtd #f #f)])
      (let loop ([rest keys])
        (unless (null? rest)
          (when (memq (car rest) (cdr rest)) (error 'defrecord "duplicate field"))
          (loop (cdr rest))))
      (make-record-descriptor name keys rtd (record-constructor rcd)
        (list->vector (map (lambda (i) (record-accessor rtd i)) (iota (length fields))))
        (and (not immutable?)
             (list->vector (map (lambda (i) (record-mutator rtd i)) (iota (length fields))))))))
  (define (record-new descriptor . values)
    (unless (record-type? descriptor) (error 'record-new "expected record type"))
    (unless (= (length values) (length (record-descriptor-keys descriptor)))
      (error 'record-new "wrong field count"))
    (apply (record-descriptor-constructor descriptor) descriptor values))
  (define (record-index descriptor key)
    (let loop ([keys (record-descriptor-keys descriptor)] [i 0])
      (cond [(null? keys) (error 'record-ref "unknown record field" key)]
            [(eq? key (car keys)) i]
            [else (loop (cdr keys) (+ i 1))])))
  (define (record-ref object key)
    (let* ([descriptor (record-type object)] [i (record-index descriptor key)])
      ((vector-ref (record-descriptor-readers descriptor) i) object)))
  (define (record-set! object key value)
    (let* ([descriptor (record-type object)] [i (record-index descriptor key)]
           [writers (record-descriptor-writers descriptor)])
      (unless writers (error 'record-set! "immutable record"))
      ((vector-ref writers i) object value) value))

  (define-record-type port-value
    (fields name input? kind host buffer (mutable at) (mutable end) (mutable position)
            (mutable closed?) (mutable eof?) callbacks extract))
  (define-record-type (instance make-instance instance?)
    (opaque #t)
    (fields type name (mutable layout) (mutable slots) (mutable migrating?)))
  (define instance-read #f)
  (define instance-write #f)
  (define instance-copy #f)
  (define (install-instance-operations! read write copy)
    (set! instance-read read) (set! instance-write write) (set! instance-copy copy))
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
  ;; Strings go straight to the port; other values are rendered with str.
  (define (buffer-push-one! b x)
    (let ([s (if (string? x) x (str x))])
      (put-string (buffer-value-port b) s)
      (buffer-value-size-set! b (+ (buffer-value-size b) (string-length s)))))
  (define buffer-push!
    (case-lambda
      [(b x)
       (unless (buffer? b) (error 'buffer-push! "expected buffer"))
       (buffer-push-one! b x)
       b]
      [(b . xs)
       (unless (buffer? b) (error 'buffer-push! "expected buffer"))
       (for-each (lambda (x) (buffer-push-one! b x)) xs)
       b]))
  (define (buffer->string b)
    (unless (buffer? b) (error 'buffer->string "expected buffer"))
    (let ([s (string-append (buffer-value-snapshot b) (get-output-string (buffer-value-port b)))])
      (buffer-value-snapshot-set! b s) s))
  (define collection-call
    (case-lambda
      [(f key)
       (cond [(record? f) (record-ref f key)]
             [(instance? f) (instance-read f key)]
             [(procedure? f) (f key)]
             [(table? f) (get-or f key (void))]
             [(array? f)
              (unless (and (integer? key) (exact? key)) (error 'call "expected integer index"))
              (get-or f key (void))]
             [(keyword? f) (if (table? key) (get-or key f (void)) (void))]
             [else (error 'call "expected callable value" f)])]
      [(f key fallback)
       (cond [(or (record? f) (instance? f)) (collection-call-general f (list key fallback))]
             [(procedure? f) (f key fallback)]
             [(table? f) (get-or f key fallback)]
             [(array? f)
              (unless (and (integer? key) (exact? key)) (error 'call "expected integer index"))
              (get-or f key fallback)]
             [(keyword? f) (if (table? key) (get-or key f fallback) fallback)]
             [else (error 'call "expected callable value" f)])]
      [(f . args) (collection-call-general f args)]))
  (define (collection-call-general f args)
    (cond [(record? f)
           (unless (= (length args) 1) (error 'record-ref "expected one keyword"))
           (record-ref f (car args))]
          [(instance? f)
           (unless (= (length args) 1) (error 'slot-ref "expected one keyword"))
           (instance-read f (car args))]
          [(procedure? f) (apply f args)]
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
  (define bytevector-ids (make-weak-eq-hashtable))
  (define next-bytevector-id 0)
  (define (bytevector-id x)
    (or (hashtable-ref bytevector-ids x #f)
        (begin (set! next-bytevector-id (+ next-bytevector-id 1))
               (hashtable-set! bytevector-ids x next-bytevector-id)
               next-bytevector-id)))
  (define (key-hash x)
    (cond [(bytevector? x) (bitwise-and #x3fffffff (bytevector-id x))]
          [(pair? x)
           (hashtable-set! frozen x #t)
           (bitwise-and #x3fffffff (+ (* 33 (key-hash (car x))) (key-hash (cdr x))))]
          [(string? x) (string-hash x)]
          [(number? x) (if (and (real? x) (nan? x)) 17 (equal-hash (if (= x 0) 0 x)))]
          [else (equal-hash x)]))
  (define-record-type (table-value make-table table?) (fields data (mutable order)))
  (define table
    (case-lambda
      [()
       (let ([t (make-table (make-hashtable key-hash key=?) '())])
         t)]
      [(k0 v0)
       (let ([t (make-table (make-hashtable key-hash key=?) '())])
         (put-one! t k0 v0)
         t)]
      [(k0 v0 k1 v1)
       (let ([t (make-table (make-hashtable key-hash key=?) '())])
         (put-one! t k0 v0)
         (put-one! t k1 v1)
         t)]
      [(k0 v0 k1 v1 k2 v2)
       (let ([t (make-table (make-hashtable key-hash key=?) '())])
         (put-one! t k0 v0)
         (put-one! t k1 v1)
         (put-one! t k2 v2)
         t)]
      [(k0 v0 k1 v1 k2 v2 k3 v3)
       (let ([t (make-table (make-hashtable key-hash key=?) '())])
         (put-one! t k0 v0)
         (put-one! t k1 v1)
         (put-one! t k2 v2)
         (put-one! t k3 v3)
         t)]
      [xs
       (unless (even? (length xs)) (error 'table "expected key/value pairs"))
       (let ([t (make-table (make-hashtable key-hash key=?) '())])
         (let loop ([xs xs])
           (unless (null? xs)
             (put-one! t (car xs) (cadr xs))
             (loop (cddr xs))))
         t)]))
  ;; Fixed arities keep lookups, which run per call in tight loops, from
  ;; packing their arguments into a list.
  (define get
    (case-lambda
      [(x key) (get-or x key (void))]
      [(x key fallback) (get-or x key fallback)]
      [args (error 'get "expected 2 or 3 arguments")]))
  (define (get-or x key fallback)
    (cond [(nil? x) fallback]
          [(table? x) (hashtable-ref (table-value-data x) key fallback)]
          [(or (array? x) (string? x) (bytevector? x))
           (if (and (integer? key) (exact? key) (<= 0 key) (< key (collection-length x)))
               (cond [(array? x) (vector-ref (array-value-storage x) key)]
                     [(string? x) (string-ref x key)]
                     [else (bytevector-u8-ref x key)])
               fallback)]
          [else (error 'get "expected nil, table, array, string, or bytevector" x)]))
  (define put!
    (case-lambda
      [(x key value) (put-check! x) (put-one! x key value) x]
      [(x . xs)
       (unless (and (pair? xs) (even? (length xs))) (error 'put! "expected key/value pairs"))
       (put-check! x)
       (let loop ([xs xs])
         (unless (null? xs)
           (put-one! x (car xs) (cadr xs))
           (loop (cddr xs))))
       x]))
  (define (put-check! x)
    (unless (or (array? x) (table? x) (bytevector? x))
      (error 'put! "expected table, array, or bytevector" x)))
  (define (put-one! x key value)
    (cond
      [(bytevector? x)
       (unless (and (fixnum? key) (fx<= 0 key) (fx< key (bytevector-length x)))
         (error 'put! "index out of range" key))
       (unless (and (fixnum? value) (fx<= 0 value 255))
         (error 'put! "expected a byte from 0 to 255" value))
       (bytevector-u8-set! x key value)]
      [(array? x)
       (unless (and (integer? key) (exact? key) (<= 0 key) (< key (array-value-size x)))
         (error 'put! "index out of range" key))
       (vector-set! (array-value-storage x) key value)]
      [else
       (let ([data (table-value-data x)])
         (cond [(nil? value)
                (when (hashtable-contains? data key)
                  (hashtable-delete! data key)
                  (table-value-order-set! x (remp (lambda (k) (key=? k key)) (table-value-order x))))]
               [else
                (unless (hashtable-contains? data key)
                  (table-value-order-set! x (cons key (table-value-order x))))
                (hashtable-set! data key value)]))]))
  (define (set-applied! object key value)
    (cond [(record? object) (record-set! object key value)]
          [(instance? object) (instance-write object key value)]
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
          [(buffer? x) (buffer-value-size x)]
          [(string? x) (string-length x)] [(bytevector? x) (bytevector-length x)]
          [else (length x)]))
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
    (keyword (cond [(record? x) "record"] [(record-type? x) "record-type"] [(port-value? x) "port"] [(instance? x) "instance"] [(buffer? x) "buffer"] [(nil? x) "nil"] [(keyword? x) "keyword"] [(array? x) "array"] [(table? x) "table"]
          [(null? x) "null"] [(pair? x) "pair"] [(boolean? x) "boolean"] [(symbol? x) "symbol"]
          [(string? x) "string"] [(and (integer? x) (exact? x)) "int"]
          [(char? x) "char"] [(eof-object? x) "eof"] [(bytevector? x) "bytevector"]
          [(number? x) "float"] [(procedure? x) "function"] [else "native"])))
  (define (write-string x port)
    (display "\"" port)
    (string-for-each
      (lambda (c)
        (let ([escape (assv (char->integer c)
                        '((10 . "\\n") (9 . "\\t") (13 . "\\r")
                          (0 . "\\0") (27 . "\\e") (34 . "\\\"") (92 . "\\\\")))])
          (if escape (display (cdr escape) port) (display c port))))
      x)
    (display "\"" port))
  ;; Chars print with the same names on both runtimes, not Chez's.
  (define char-names
    '((#\space . "space") (#\newline . "newline") (#\tab . "tab") (#\return . "return")
      (#\nul . "nul")))
  (define (write-char-literal c port)
    (let ([named (assv c char-names)] [n (char->integer c)])
      (display "#\\" port)
      (cond [named (display (cdr named) port)]
            [(or (< n 32) (= n 127)) (display "x" port) (display (string-downcase (number->string n 16)) port)]
            [else (display c port)])))
  (define (render x port display? depth)
    (define (items xs)
      (unless (null? xs)
        (render (car xs) port #f (+ depth 1))
        (unless (null? (cdr xs)) (display " " port) (items (cdr xs)))))
    (cond [(or (record? x) (record-type? x))
           (display (if (record? x) "#<record " "#<record-type ") port)
           (display (record-descriptor-name (if (record? x) (record-type x) x)) port)
           (display ">" port)]
          [(instance? x)
           (display "#<" port) (display (instance-name x) port) (display ">" port)]
          [(> depth 100) (display "..." port)]
          [(buffer? x)
           (if display? (display (buffer->string x) port)
               (begin (display "#<buffer " port) (write-string (buffer->string x) port) (display ">" port)))]
          [(string? x) (if display? (display x port) (write-string x port))]
          [(nil? x) (display "nil" port)]
          [(char? x) (if display? (display x port) (write-char-literal x port))]
          [(eof-object? x) (display "#<eof>" port)]
          [(port-value? x)
           (display "#<port " port)
           (write (port-value-name x) port)
           (display (if (port-value-input? x) " input>" " output>") port)]
          [(bytevector? x)
           (display "#u8(" port)
           (let ([n (bytevector-length x)])
             (do ([i 0 (+ i 1)]) ((= i n))
               (unless (= i 0) (display " " port))
               (display (bytevector-u8-ref x i) port)))
           (display ")" port)]
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
