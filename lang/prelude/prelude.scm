;; prelude.scm -- the derived core (spec 10.x), loaded through the real
;; expander, so defmacro and everything native is available.
;;
;; Runtime dependencies: cons car cdr list append length reverse for-each
;; get put! push! pop! keys values copy table array apply identity
;; arithmetic, comparisons, string natives (10.6), type predicates, not,
;; eq? equal?, error, condition-of-type?, gensym.
;; Additionally assumed for sort: (string<? a b) lexicographic order.

;; --- internal helpers -------------------------------------------------------

(define- (to-list seq)
  "Coerce any sequence to a proper list."
  (cond ((nil? seq) '())
        ((array? seq) (array-to-list seq))
        ((list? seq) seq)
        (else (error "to-list: expected sequence"))))

(define- (finish-like source values)
  (if (array? source) (list-to-array values) values))

(define- (builder-like source) (if (array? source) (array) '()))

(define- (builder-add builder value)
  (if (array? builder) (begin (push! builder value) builder) (cons value builder)))

(define- (builder-finish builder)
  (if (array? builder) builder (reverse builder)))

(define- (slice seq start count)
  (if (array? seq)
      (let ((out (array)) (i start)
            (end (if (nil? count) (length seq) (min (+ start count) (length seq)))))
        (while (< i end)
          (push! out (get seq i))
          (set! i (+ i 1)))
        out)
      (let ((s (to-list seq)) (skip start))
        (while (and (pair? s) (> skip 0))
          (set! s (cdr s))
          (set! skip (- skip 1)))
        (if (nil? count)
            s
            (let ((acc '()) (left count))
              (while (and (pair? s) (> left 0))
                (set! acc (cons (car s) acc))
                (set! s (cdr s))
                (set! left (- left 1)))
              (reverse acc))))))

(define- (require-int who value)
  (if (int? value)
      value
      (error (str who ": expected integer"))))

;; --- macros (10.11) ---------------------------------------------------------

(defmacro defn (name params . body)
  (cons 'define (cons (cons name params) body)))

(defmacro when (test . body)
  `(if ,test (begin ,@body) nil))

(defmacro unless (test . body)
  `(if ,test nil (begin ,@body)))

(defmacro -> (x . forms)
  (let ((acc x) (s forms))
    (while (pair? s)
      (let ((f (car s)))
        (set! acc (if (pair? f)
                      (cons (car f) (cons acc (cdr f)))
                      (list f acc))))
      (set! s (cdr s)))
    acc))

(defmacro ->> (x . forms)
  (let ((acc x) (s forms))
    (while (pair? s)
      (let ((f (car s)))
        (set! acc (if (pair? f)
                      (append f (list acc))
                      (list f acc))))
      (set! s (cdr s)))
    acc))

(defmacro dotimes (spec . body)
  (let ((sp (if (eq? (car spec) 'array) (cdr spec) spec)))
    (let ((i (car sp)) (n (car (cdr sp))) (lim (gensym "lim")))
      `(let ((,lim ,n) (,i 0))
         (while (< ,i ,lim)
           ,@body
           (set! ,i (+ ,i 1)))
         nil))))

;; --- sequences (10.4) -------------------------------------------------------

(define (caar p) (car (car p)))
(define (cadr p) (car (cdr p)))
(define (cddr p) (cdr (cdr p)))

(define (list-to-array value)
  (unless (list? value) (error "list-to-array: expected proper list"))
  (let ((out (array)))
    (for-each (fn (x) (push! out x)) value)
    out))

(define (array-to-list value)
  (unless (array? value) (error "array-to-list: expected array"))
  (let ((acc '()) (i (- (length value) 1)))
    (while (>= i 0)
      (set! acc (cons (get value i) acc))
      (set! i (- i 1)))
    acc))

(define (first seq)
  (cond ((nil? seq) nil)
        ((null? seq) nil)
        ((pair? seq) (car seq))
        ((array? seq) (if (> (length seq) 0) (get seq 0) nil))
        ((string? seq) (if (> (string-length seq) 0) (substring seq 0 1) nil))
        (else (error "first: not a sequence"))))

(define (rest seq)
  "All but the first element; kind-preserving; empty is ()."
  (cond ((nil? seq) '())
        ((null? seq) '())
        ((pair? seq) (cdr seq))
        ((array? seq) (drop 1 seq))
        (else (error "rest: not a sequence"))))

(define (nth seq i)
  (require-int "nth" i)
  (cond ((nil? seq) nil)
        ((null? seq) nil)
        ((array? seq) (if (and (>= i 0) (< i (length seq))) (get seq i) nil))
        ((pair? seq)
         (if (< i 0)
             nil
             (let ((s seq) (k i))
               (while (and (pair? s) (> k 0))
                 (set! s (cdr s))
                 (set! k (- k 1)))
               (if (pair? s) (car s) nil))))
        (else (error "nth: not a sequence"))))

(define (second seq) (nth seq 1))
(define (third seq) (nth seq 2))

(define (last seq)
  (cond ((nil? seq) nil)
        ((null? seq) nil)
        ((array? seq) (if (> (length seq) 0) (get seq (- (length seq) 1)) nil))
        ((pair? seq)
         (let ((s seq))
           (while (pair? (cdr s)) (set! s (cdr s)))
           (car s)))
        (else (error "last: not a sequence"))))

(define (count coll)
  (if (nil? coll) 0 (length coll)))

(define (empty? coll)
  (cond ((nil? coll) #t)
        ((null? coll) #t)
        ((pair? coll) #f)
        ((string? coll) (= (string-length coll) 0))
        ((buffer? coll) (= (length coll) 0))
        ((or (array? coll) (table? coll)) (= (length coll) 0))
        (else (error "empty?: unsupported type"))))

(define (map f seq)
  "Kind-preserving: list in, list out; array in, array out; nil -> ()."
  (let ((out (builder-like seq)))
    (for-each (fn (x) (set! out (builder-add out (f x)))) seq)
    (builder-finish out)))

(define (filter pred seq)
  (let ((out (builder-like seq)))
    (for-each (fn (x) (when (pred x) (set! out (builder-add out x)))) seq)
    (builder-finish out)))

(define (reduce f init seq)
  (let ((acc init))
    (for-each (fn (x) (set! acc (f acc x))) seq)
    acc))

(define (range . args)
  (let ((n (length args)) (a 0) (b 0) (step 1))
    (cond ((= n 1) (set! b (nth args 0)))
          ((= n 2) (begin (set! a (nth args 0)) (set! b (nth args 1))))
          ((= n 3) (begin (set! a (nth args 0)) (set! b (nth args 1))
                          (set! step (nth args 2))))
          (else (error "range: expected 1 to 3 arguments")))
    (require-int "range" a)
    (require-int "range" b)
    (require-int "range" step)
    (when (= step 0) (error "range: step must be nonzero"))
    (let ((out (array)) (i a))
      (if (> step 0)
          (while (< i b) (push! out i) (set! i (+ i step)))
          (while (> i b) (push! out i) (set! i (+ i step))))
      out)))

(define (concat . seqs)
  "Concatenation; kind follows the first argument (nil -> list)."
  (let ((out (builder-like (if (pair? seqs) (car seqs) nil))))
    (for-each (fn (s) (for-each (fn (x) (set! out (builder-add out x))) s)) seqs)
    (builder-finish out)))

(define (take n seq)
  (require-int "take" n)
  (slice seq 0 (max n 0)))

(define (drop n seq)
  (require-int "drop" n)
  (slice seq (max n 0) nil))

(define (contains? coll x)
  (cond ((nil? coll) #f)
        ((table? coll) (not (nil? (get coll x))))
        ((string? coll) (string-contains? coll x))
        ((or (null? coll) (pair? coll) (array? coll))
         (let ((found #f))
           (for-each (fn (e) (when (equal? e x) (set! found #t))) coll)
           found))
        (else (error "contains?: unsupported collection"))))

;; --- sorting ----------------------------------------------------------------

(define- (sort-class k)
  (cond ((number? k) :number)
        ((string? k) :string)
        ((symbol? k) :symbol)
        ((keyword? k) :keyword)
        (else (error "sort: keys must be numbers, strings, symbols, or keywords"))))

(define- (key-less? a b)
  (if (number? a)
      (< a b)
      (string<? (name a) (name b))))   ; strings/symbols/keywords, lexicographic

;; stable merge of two lists of (key . elem) pairs
(define- (merge-runs a b)
  (cond ((null? a) b)
        ((null? b) a)
        ((key-less? (car (car b)) (car (car a)))
         (cons (car b) (merge-runs a (cdr b))))
        (else (cons (car a) (merge-runs (cdr a) b)))))

(define- (msort lst n)
  (if (< n 2)
      lst
      (let ((half (quotient n 2)))
        (merge-runs (msort (take half lst) half)
                    (msort (drop half lst) (- n half))))))

(define (sort-by keyfn seq)
  "Stable sort by derived keys; keys must all be one orderable class."
  (let ((lst (map (fn (x) (cons (keyfn x) x)) (to-list seq))))
    (when (pair? lst)
      (let ((cls (sort-class (car (car lst)))))
        (for-each (fn (p)
                    (unless (eq? (sort-class (car p)) cls)
                      (error "sort: mixed key classes")))
                  lst)))
    (let ((res (map cdr (msort lst (length lst)))))
      (finish-like seq res))))

(define (sort seq) (sort-by identity seq))

(define (group-by keyfn seq)
  "Table of key -> array of elements, in first-seen key order."
  (let ((t (table)))
    (for-each (fn (x)
                (let ((k (keyfn x)))
                  (let ((a (get t k)))
                    (when (nil? a)
                      (set! a (array))
                      (put! t k a))
                    (push! a x))))
              seq)
    t))

(define (frequencies seq)
  (let ((t (table)))
    (for-each (fn (x) (put! t x (+ 1 (get t x 0)))) seq)
    t))

;; --- tables (10.5) ----------------------------------------------------------

(define (assoc coll . kvs)
  "put! on a shallow copy; (assoc nil ...) builds a fresh table."
  (let ((c (if (nil? coll) (table) (copy coll))))
    (let ((s kvs))
      (while (pair? s)
        (when (null? (cdr s)) (error "assoc: odd number of key/value arguments"))
        (put! c (car s) (car (cdr s)))
        (set! s (cdr (cdr s)))))
    c))

(define (dissoc coll . ks)
  (let ((c (if (nil? coll) (table) (copy coll))))
    (for-each (fn (k) (put! c k nil)) ks)
    c))

(define (update coll k f . args)
  (assoc coll k (apply f (get coll k) args)))

(define (merge . ts)
  "New table, left to right; nil arguments skipped."
  (let ((out (table)))
    (for-each (fn (t)
                (unless (nil? t)
                  (for-each (fn (k) (put! out k (get t k))) (keys t))))
              ts)
    out))

(define (get-in coll path . more)
  "get folded over a sequence of keys."
  (when (> (length more) 1) (error "get-in: expected 2 or 3 arguments"))
  (let ((default (if (pair? more) (car more) nil))
        (c coll))
    (for-each (fn (k) (set! c (get c k))) path)
    (if (nil? c) default c)))

;; --- conditions (10.9) ------------------------------------------------------

(define (condition-type c)
  (if (table? c) (get c :type) nil))

(define (condition-message c)
  (if (table? c) (get c :message) nil))

(define (error? c)
  (if (and (table? c) (not (nil? (get c :type)))) #t #f))

(define (type-pred t)
  (lambda (c) (condition-of-type? c t)))

;; --- functional (10.8) ------------------------------------------------------

(define (partial f . pre)
  (lambda args (apply f (append pre args))))

(define (comp . fs)
  "Right-to-left composition; (comp) is identity."
  (cond ((null? fs) identity)
        ((null? (cdr fs)) (car fs))
        (else
         (let ((inner (apply comp (cdr fs))))
           (lambda args ((car fs) (apply inner args)))))))
