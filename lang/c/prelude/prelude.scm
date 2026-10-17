;; prelude.scm -- the derived core (spec 10.x), loaded through the real
;; expander, so defmacro and everything native is available.
;;
;; Runtime dependencies: cons car cdr list append length reverse for-each
;; get put! push! pop! keys values copy table array apply identity
;; arithmetic, comparisons, string natives (10.6), type predicates, not,
;; eq? equal?, error, condition-of-type?, gensym.
;; Additionally assumed for sort: (string<? a b) lexicographic order.

;; --- internal helpers -------------------------------------------------------

(def- (to-list seq)
  "Coerce any sequence to a proper list."
  (cond ((nil? seq) '())
        ((array? seq) (array-to-list seq))
        ((list? seq) seq)
        (else (error "to-list: expected sequence"))))

(def- (finish-like source values)
  (if (array? source) (list-to-array values) values))

(def- (builder-like source) (if (array? source) (array) '()))

(def- (builder-add builder value)
  (if (array? builder) (begin (push! builder value) builder) (cons value builder)))

(def- (builder-finish builder)
  (if (array? builder) builder (reverse builder)))

(def- (slice seq start count)
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

(def- (require-int who value)
  (if (int? value)
      value
      (error (str who ": expected integer"))))

;; --- macros (10.11) ---------------------------------------------------------

(defmacro defn (name params . body)
  "(defn name parameters body ...)\nDefine a named procedure; shorthand for define with a procedure head."
  (cons 'def (cons (cons name params) body)))

(defmacro when (test . body)
  "(when test body ...)\nEvaluate body when test is truthy; otherwise return nil."
  `(if ,test (begin ,@body) nil))

(defmacro unless (test . body)
  "(unless test body ...)\nEvaluate body when test is falsy; otherwise return nil."
  `(if ,test nil (begin ,@body)))

(defmacro -> (x . forms)
  "(-> value form ...)\nThread value into the first argument position of each form in turn; a bare name is called with the value."
  (let ((acc x) (s forms))
    (while (pair? s)
      (let ((f (car s)))
        (set! acc (if (pair? f)
                      (cons (car f) (cons acc (cdr f)))
                      (list f acc))))
      (set! s (cdr s)))
    acc))

(defmacro ->> (x . forms)
  "(->> value form ...)\nThread value into the last argument position of each form in turn."
  (let ((acc x) (s forms))
    (while (pair? s)
      (let ((f (car s)))
        (set! acc (if (pair? f)
                      (append f (list acc))
                      (list f acc))))
      (set! s (cdr s)))
    acc))

(defmacro dotimes (spec . body)
  "(dotimes (index count) body ...)\nEvaluate count once, then run body with index from 0 below count. Return nil."
  (let ((sp (if (eq? (car spec) 'array) (cdr spec) spec)))
    (let ((i (car sp)) (n (car (cdr sp))) (lim (gensym "lim")))
      `(let ((,lim ,n) (,i 0))
         (while (< ,i ,lim)
           ,@body
           (set! ,i (+ ,i 1)))
         nil))))

;; --- sequences (10.4) -------------------------------------------------------

(def (caar p)
  "(caar pair)\nEquivalent to (car (car pair))."
  (car (car p)))
(def (cadr p)
  "(cadr pair)\nEquivalent to (car (cdr pair))."
  (car (cdr p)))
(def (cddr p)
  "(cddr pair)\nEquivalent to (cdr (cdr pair))."
  (cdr (cdr p)))

;; Scheme list helpers, matching the Chez runtime's names.
(def (cdar p)
  "(cdar pair)\nEquivalent to (cdr (car pair))."
  (cdr (car p)))
(def (caaar p)
  "(caaar pair)\nEquivalent to (car (car (car pair)))."
  (car (car (car p))))
(def (caadr p)
  "(caadr pair)\nEquivalent to (car (car (cdr pair)))."
  (car (car (cdr p))))
(def (cadar p)
  "(cadar pair)\nEquivalent to (car (cdr (car pair)))."
  (car (cdr (car p))))
(def (caddr p)
  "(caddr pair)\nEquivalent to (car (cdr (cdr pair)))."
  (car (cdr (cdr p))))
(def (cdaar p)
  "(cdaar pair)\nEquivalent to (cdr (car (car pair)))."
  (cdr (car (car p))))
(def (cdadr p)
  "(cdadr pair)\nEquivalent to (cdr (car (cdr pair)))."
  (cdr (car (cdr p))))
(def (cddar p)
  "(cddar pair)\nEquivalent to (cdr (cdr (car pair)))."
  (cdr (cdr (car p))))
(def (cdddr p)
  "(cdddr pair)\nEquivalent to (cdr (cdr (cdr pair)))."
  (cdr (cdr (cdr p))))
(def (cadddr p)
  "(cadddr pair)\nEquivalent to (car (cdr (cdr (cdr pair))))."
  (car (cdr (cdr (cdr p)))))

(def (list-tail lst k)
  "(list-tail list k)\nThe list after dropping its first k pairs."
  (let loop ((l lst) (n (require-int "list-tail" k)))
    (cond ((= n 0) l)
          ((pair? l) (loop (cdr l) (- n 1)))
          (else (error "list-tail: index out of range")))))

(def (list-ref lst k)
  "(list-ref list k)\nThe element at index k of a list."
  (let ((tail (list-tail lst k)))
    (if (pair? tail) (car tail) (error "list-ref: index out of range"))))

(def (memq x lst)
  "(memq value list)\nThe first tail of list whose car is eq? to value, or #f."
  (cond ((not (pair? lst)) #f)
        ((eq? x (car lst)) lst)
        (else (memq x (cdr lst)))))

(def (member x lst)
  "(member value list)\nThe first tail of list whose car is equal? to value, or #f."
  (cond ((not (pair? lst)) #f)
        ((equal? x (car lst)) lst)
        (else (member x (cdr lst)))))

(def (assq key alist)
  "(assq key alist)\nThe first pair in alist whose car is eq? to key, or #f."
  (cond ((not (pair? alist)) #f)
        ((eq? key (car (car alist))) (car alist))
        (else (assq key (cdr alist)))))

(def- (heads lists) (if (pair? lists) (cons (car (car lists)) (heads (cdr lists))) '()))
(def- (tails lists) (if (pair? lists) (cons (cdr (car lists)) (tails (cdr lists))) '()))
(def- (any-empty? lists)
  (and (pair? lists) (or (not (pair? (car lists))) (any-empty? (cdr lists)))))

(def (fold-left f init lst & more)
  "(fold-left procedure initial list ...)\nCombine from the left: (procedure accumulator element ...) for each position."
  (let loop ((acc init) (lists (cons lst more)))
    (if (any-empty? lists) acc (loop (apply f acc (heads lists)) (tails lists)))))

(def (fold-right f init lst & more)
  "(fold-right procedure initial list ...)\nCombine from the right: (procedure element ... accumulator) for each position."
  (let loop ((lists (cons lst more)))
    (if (any-empty? lists)
        init
        (apply f (append (heads lists) (list (loop (tails lists))))))))

(def (exists f lst & more)
  "(exists predicate list ...)\nThe first truthy result of predicate across the lists, or #f."
  (let loop ((lists (cons lst more)))
    (if (any-empty? lists)
        #f
        (or (apply f (heads lists)) (loop (tails lists))))))

(def (for-all f lst & more)
  "(for-all predicate list ...)\nThe last result if predicate is truthy for every position, else the first falsy result."
  (let loop ((lists (cons lst more)) (last #t))
    (if (any-empty? lists)
        last
        (let ((v (apply f (heads lists))))
          (if v (loop (tails lists) v) v)))))

(def (positive? x)
  "(positive? number)\nTest whether number is greater than zero."
  (> x 0))
(def (negative? x)
  "(negative? number)\nTest whether number is less than zero."
  (< x 0))

(def (gcd & xs)
  "(gcd integer ...)\nGreatest common divisor; no arguments gives 0."
  (let loop ((acc 0) (rest xs))
    (if (pair? rest)
        (loop (let euclid ((a (abs acc)) (b (abs (car rest))))
                (if (= b 0) a (euclid b (remainder a b))))
              (cdr rest))
        acc)))
(def (lcm & xs)
  "(lcm integer ...)\nLeast common multiple; no arguments gives 1."
  (let loop ((acc 1) (rest xs))
    (if (pair? rest)
        (loop (if (or (= acc 0) (= (car rest) 0))
                  0
                  (quotient (abs (* acc (car rest))) (gcd acc (car rest))))
              (cdr rest))
        acc)))

(def (keyword->string k)
  "(keyword->string keyword)\nThe name of a keyword, without its colon."
  (if (keyword? k) (name k) (error "keyword->string: expected keyword")))

(def- (string-chain who ok? a b more)
  (if (not (and (string? a) (string? b))) (error (str who ": expected strings")))
  (and (ok? a b) (or (null? more) (string-chain who ok? b (car more) (cdr more)))))
(def (string=? a b & more)
  "(string=? string string ...)\nTest whether all strings are equal."
  (string-chain "string=?" equal? a b more))
(def (string>? a b & more)
  "(string>? string string ...)\nTest whether strings are in strictly decreasing order."
  (string-chain "string>?" (fn (x y) (string<? y x)) a b more))
(def (string<=? a b & more)
  "(string<=? string string ...)\nTest whether strings are in nondecreasing order."
  (string-chain "string<=?" (fn (x y) (not (string<? y x))) a b more))
(def (string>=? a b & more)
  "(string>=? string string ...)\nTest whether strings are in nonincreasing order."
  (string-chain "string>=?" (fn (x y) (not (string<? x y))) a b more))

(def (list-to-array value)
  "(list-to-array sequence)\nConvert a proper list to a new array; other input types raise an error."
  (unless (list? value) (error "list-to-array: expected proper list"))
  (let ((out (array)))
    (for-each (fn (x) (push! out x)) value)
    out))

(def (array-to-list value)
  "(array-to-list sequence)\nConvert an array to a new list; other input types raise an error."
  (unless (array? value) (error "array-to-list: expected array"))
  (let ((acc '()) (i (- (length value) 1)))
    (while (>= i 0)
      (set! acc (cons (get value i) acc))
      (set! i (- i 1)))
    acc))

(def (first seq)
  "(first sequence)\nReturn the first element, or nil. Also accepts strings, returning a one-character string."
  (cond ((nil? seq) nil)
        ((null? seq) nil)
        ((pair? seq) (car seq))
        ((array? seq) (if (> (length seq) 0) (get seq 0) nil))
        ((string? seq) (if (> (string-length seq) 0) (substring seq 0 1) nil))
        (else (error "first: not a sequence"))))

(def (rest seq)
  "All but the first element; kind-preserving; empty is ()."
  (cond ((nil? seq) '())
        ((null? seq) '())
        ((pair? seq) (cdr seq))
        ((array? seq) (drop 1 seq))
        (else (error "rest: not a sequence"))))

(def (nth seq i)
  "(nth sequence index)\nReturn the element at a zero-based integer index, or nil out of range. Accepts lists, arrays, and nil."
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

(def (second seq)
  "(second sequence)\nReturn element at index 1, or nil."
  (nth seq 1))
(def (third seq)
  "(third sequence)\nReturn element at index 2, or nil."
  (nth seq 2))

(def (last seq)
  "(last sequence)\nReturn the last element, or nil."
  (cond ((nil? seq) nil)
        ((null? seq) nil)
        ((array? seq) (if (> (length seq) 0) (get seq (- (length seq) 1)) nil))
        ((pair? seq)
         (let ((s seq))
           (while (pair? (cdr s)) (set! s (cdr s)))
           (car s)))
        (else (error "last: not a sequence"))))

(def (count coll)
  "(count collection)\nLike length, but nil counts as zero."
  (if (nil? coll) 0 (length coll)))

(def (empty? coll)
  "(empty? collection)\nTest whether a sequence, table, string, or buffer has no elements. nil is empty."
  (cond ((nil? coll) #t)
        ((null? coll) #t)
        ((pair? coll) #f)
        ((string? coll) (= (string-length coll) 0))
        ((buffer? coll) (= (length coll) 0))
        ((or (array? coll) (table? coll)) (= (length coll) 0))
        (else (error "empty?: unsupported type"))))

(def (map f seq)
  "Kind-preserving: list in, list out; array in, array out; nil -> ()."
  (let ((out (builder-like seq)))
    (for-each (fn (x) (set! out (builder-add out (f x)))) seq)
    (builder-finish out)))

(def (filter pred seq)
  "(filter predicate sequence)\nKeep elements for which predicate returns a truthy value; preserve list/array kind."
  (let ((out (builder-like seq)))
    (for-each (fn (x) (when (pred x) (set! out (builder-add out x)))) seq)
    (builder-finish out)))

(def (reduce f init seq)
  "(reduce procedure initial sequence)\nFold left, calling (procedure accumulator element) for each element. Empty sequences return initial."
  (let ((acc init))
    (for-each (fn (x) (set! acc (f acc x))) seq)
    acc))

(def (range . args)
  "(range end) / (range start end [step])\nReturn an array of integers toward the exclusive end. Defaults: start 0, step 1. Step must be nonzero and may be negative."
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

(def (concat . seqs)
  "Concatenation; kind follows the first argument (nil -> list)."
  (let ((out (builder-like (if (pair? seqs) (car seqs) nil))))
    (for-each (fn (s) (for-each (fn (x) (set! out (builder-add out x))) s)) seqs)
    (builder-finish out)))

(def (take n seq)
  "(take count sequence)\nReturn the first count elements. Count clamps to the available range; preserve list/array kind."
  (require-int "take" n)
  (slice seq 0 (max n 0)))

(def (drop n seq)
  "(drop count sequence)\nReturn the elements after the first count. Count clamps to the available range; preserve list/array kind."
  (require-int "drop" n)
  (slice seq (max n 0) nil))

(def (contains? coll x)
  "(contains? collection value)\nTest table key presence, equal? membership in a sequence, or substring presence in a string. nil returns #f."
  (cond ((nil? coll) #f)
        ((table? coll) (not (nil? (get coll x))))
        ((string? coll) (string-contains? coll x))
        ((or (null? coll) (pair? coll) (array? coll))
         (let ((found #f))
           (for-each (fn (e) (when (equal? e x) (set! found #t))) coll)
           found))
        (else (error "contains?: unsupported collection"))))

;; --- sorting ----------------------------------------------------------------

(def- (sort-class k)
  (cond ((number? k) :number)
        ((string? k) :string)
        ((symbol? k) :symbol)
        ((keyword? k) :keyword)
        (else (error "sort: keys must be numbers, strings, symbols, or keywords"))))

(def- (key-less? a b)
  (if (number? a)
      (< a b)
      (string<? (name a) (name b))))   ; strings/symbols/keywords, lexicographic

;; stable merge of two lists of (key . elem) pairs
(def- (merge-runs a b)
  (cond ((null? a) b)
        ((null? b) a)
        ((key-less? (car (car b)) (car (car a)))
         (cons (car b) (merge-runs a (cdr b))))
        (else (cons (car a) (merge-runs (cdr a) b)))))

(def- (msort lst n)
  (if (< n 2)
      lst
      (let ((half (quotient n 2)))
        (merge-runs (msort (take half lst) half)
                    (msort (drop half lst) (- n half))))))

(def (sort-by keyfn seq)
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

(def (sort seq)
  "(sort sequence)\nReturn an ascending sorted copy, preserving list/array kind. Keys must belong to a supported, consistent orderable class."
  (sort-by identity seq))

(def (group-by keyfn seq)
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

(def (frequencies seq)
  "(frequencies sequence)\nReturn a table mapping each distinct element to its occurrence count."
  (let ((t (table)))
    (for-each (fn (x) (put! t x (+ 1 (get t x 0)))) seq)
    t))

;; --- tables (10.5) ----------------------------------------------------------

(def (assoc coll . kvs)
  "put! on a shallow copy; (assoc nil ...) builds a fresh table."
  (let ((c (if (nil? coll) (table) (copy coll))))
    (let ((s kvs))
      (while (pair? s)
        (when (null? (cdr s)) (error "assoc: odd number of key/value arguments"))
        (put! c (car s) (car (cdr s)))
        (set! s (cdr (cdr s)))))
    c))

(def (dissoc coll . ks)
  "(dissoc collection key ...)\nReturn a shallow copy without the specified keys."
  (let ((c (if (nil? coll) (table) (copy coll))))
    (for-each (fn (k) (put! c k nil)) ks)
    c))

(def (update coll k f . args)
  "(update collection key procedure argument ...)\nCopy the collection and replace the entry with (procedure (get collection key) argument ...)."
  (assoc coll k (apply f (get coll k) args)))

(def (merge . ts)
  "New table, left to right; nil arguments skipped."
  (let ((out (table)))
    (for-each (fn (t)
                (unless (nil? t)
                  (for-each (fn (k) (put! out k (get t k))) (keys t))))
              ts)
    out))

(def (get-in coll path . more)
  "get folded over a sequence of keys."
  (when (> (length more) 1) (error "get-in: expected 2 or 3 arguments"))
  (let ((default (if (pair? more) (car more) nil))
        (c coll))
    (for-each (fn (k) (set! c (get c k))) path)
    (if (nil? c) default c)))

;; --- conditions (10.9) ------------------------------------------------------

(def (condition-type c)
  "(condition-type condition)\nReturn a condition table's :type, or nil."
  (if (table? c) (get c :type) nil))

(def (condition-message c)
  "(condition-message condition)\nReturn a condition table's :message, or nil."
  (if (table? c) (get c :message) nil))

(def (error? c)
  "(error? value)\nTest whether value is a table with a :type entry."
  (if (and (table? c) (not (nil? (get c :type)))) #t #f))

(def (type-pred t)
  "(type-pred type)\nBuild a predicate for conditions of type or its registered subtypes."
  (fn (c) (condition-of-type? c t)))

;; --- functional (10.8) ------------------------------------------------------

(def (partial f . pre)
  "(partial procedure argument ...)\nReturn a procedure that prepends the supplied arguments to its later arguments."
  (fn args (apply f (append pre args))))

(def (comp . fs)
  "Right-to-left composition; (comp) is identity."
  (cond ((null? fs) identity)
        ((null? (cdr fs)) (car fs))
        (else
         (let ((inner (apply comp (cdr fs))))
           (fn args ((car fs) (apply inner args)))))))
