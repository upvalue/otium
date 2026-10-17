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

(defmacro when (test . body)
  "(when test body ...)\nEvaluate body when test is truthy; otherwise return nil."
  `(if ,test (begin ,@body) nil))

(defmacro unless (test . body)
  "(unless test body ...)\nEvaluate body when test is falsy; otherwise return nil."
  `(if ,test nil (begin ,@body)))

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

;; --- tables (10.5) ----------------------------------------------------------

(def (put coll . kvs)
  "put! on a shallow copy; (assoc nil ...) builds a fresh table."
  (let ((c (if (nil? coll) (table) (copy coll))))
    (let ((s kvs))
      (while (pair? s)
        (when (null? (cdr s)) (error "put: odd number of key/value arguments"))
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
  (put coll k (apply f (get coll k) args)))

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

(def (memq x xs)
  "Return the list tail starting with an eq? match, or #f."
  (cond ((null? xs) #f) ((eq? x (car xs)) xs) (else (memq x (cdr xs)))))
(def (member x xs)
  "Return the list tail starting with an equal? match, or #f."
  (cond ((null? xs) #f) ((equal? x (car xs)) xs) (else (member x (cdr xs)))))
(def (assq x xs)
  "Find an association-list entry by eq?, or return #f."
  (cond ((null? xs) #f) ((eq? x (car (car xs))) (car xs)) (else (assq x (cdr xs)))))
(def (assoc x xs)
  "Find an association-list entry by equal?, or return #f."
  (cond ((null? xs) #f) ((equal? x (car (car xs))) (car xs)) (else (assoc x (cdr xs)))))
(def- (heads xs) (if (null? xs) '() (cons (car (car xs)) (heads (cdr xs)))))
(def- (tails xs) (if (null? xs) '() (cons (cdr (car xs)) (tails (cdr xs)))))
(def- (any-empty? xs)
  (and (pair? xs) (or (null? (car xs)) (any-empty? (cdr xs)))))
(def (fold-left f init xs & more)
  "Fold proper lists left, stopping at the shortest list."
  (let loop ((lists (cons xs more)) (acc init))
    (if (any-empty? lists) acc (loop (tails lists) (apply f acc (heads lists))))))
(def (fold-right f init xs & more)
  "Fold proper lists right, stopping at the shortest list."
  (let loop ((lists (cons xs more)))
    (if (any-empty? lists) init (apply f (append (heads lists) (list (loop (tails lists))))))))
(def (exists f xs & more)
  "Return the first truthy predicate result, stopping at the shortest list."
  (let loop ((lists (cons xs more)))
    (if (any-empty? lists) #f (or (apply f (heads lists)) (loop (tails lists))))))
(def (for-all f xs & more)
  "Return the last predicate result, or the first falsy result; stop at the shortest list."
  (let loop ((lists (cons xs more)) (last #t))
    (if (any-empty? lists) last
        (let ((value (apply f (heads lists)))) (if value (loop (tails lists) value) value)))))

(def (cdar p)
  "(cdar pair)\nCompose car and cdr."
  (cdr (car p)))
(def (caaar p)
  "(caaar pair)\nCompose car and cdr."
  (car (car (car p))))
(def (caadr p)
  "(caadr pair)\nCompose car and cdr."
  (car (car (cdr p))))
(def (cadar p)
  "(cadar pair)\nCompose car and cdr."
  (car (cdr (car p))))
(def (caddr p)
  "(caddr pair)\nCompose car and cdr."
  (car (cdr (cdr p))))
(def (cdaar p)
  "(cdaar pair)\nCompose car and cdr."
  (cdr (car (car p))))
(def (cdadr p)
  "(cdadr pair)\nCompose car and cdr."
  (cdr (car (cdr p))))
(def (cddar p)
  "(cddar pair)\nCompose car and cdr."
  (cdr (cdr (car p))))
(def (cdddr p)
  "(cdddr pair)\nCompose car and cdr."
  (cdr (cdr (cdr p))))
(def (cadddr p)
  "(cadddr pair)\nCompose car and cdr."
  (car (cdr (cdr (cdr p)))))

(def (list-tail xs n)
  "Return the tail at a nonnegative integer index; an index beyond the list is an error."
  (require-int "list-tail" n)
  (when (< n 0) (error "list-tail: negative index"))
  (if (= n 0) xs (list-tail (cdr xs) (- n 1))))
(def (list-ref xs n)
  "Return the list element at a nonnegative integer index; an absent index is an error."
  (car (list-tail xs n)))
(def positive? pos?)
(def negative? neg?)
(def (gcd & xs)
  "Return the nonnegative greatest common divisor of integer arguments."
  (fold-left (fn (a b)
    (require-int "gcd" b)
    (let loop ((a a) (b (abs b))) (if (= b 0) a (loop b (remainder a b))))) 0 xs))
(def (lcm & xs)
  "Return the nonnegative least common multiple of integer arguments."
  (fold-left (fn (a b)
    (require-int "lcm" b)
    (if (or (= a 0) (= b 0)) 0 (abs (* (quotient a (gcd a b)) b)))) 1 xs))
(def- (string-chain pred a b more)
  (unless (and (string? a) (string? b)) (error "expected strings"))
  (and (pred a b) (if (pair? more) (string-chain pred b (car more) (cdr more)) #t)))
(def (string=? a b & more)
  "Test equality of successive strings."
  (string-chain equal? a b more))
(def (string>? a b & more)
  "Test descending lexicographic order of successive strings."
  (string-chain (fn (a b) (string<? b a)) a b more))
(def (string<=? a b & more)
  "Test nondecreasing lexicographic order of successive strings."
  (string-chain (fn (a b) (not (string<? b a))) a b more))
(def (string>=? a b & more)
  "Test nonincreasing lexicographic order of successive strings."
  (string-chain (fn (a b) (not (string<? a b))) a b more))
