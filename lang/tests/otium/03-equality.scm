; Equality matrix per spec 2.4: = (numeric), equal? (structural,
; type-strict), eq? (identity).

; = : numeric across int/float
(println "num-eq-mixed:" (= 1 1.0))
(println "num-eq-chain:" (= 2 2.0 2))
(println "num-neq:" (= 1 2))

; equal? : type-strict on numbers
(println "equal-mixed-num:" (equal? 1 1.0))
(println "equal-same-int:" (equal? 1 1))
(println "equal-same-float:" (equal? 1.5 1.5))

; equal? : deep on immutable structure
(println "equal-pairs:" (equal? '(1 (2 3) . 4) '(1 (2 3) . 4)))
(println "equal-pairs-diff:" (equal? '(1 2) '(1 3)))
(println "equal-strings:" (equal? "abc" "abc"))
(println "equal-strings-diff:" (equal? "abc" "abd"))
(println "equal-symbols:" (equal? 'foo 'foo))
(println "equal-keywords:" (equal? :foo :foo))
(println "equal-nested-mixed:" (equal? '(1 "a" :k) '(1 "a" :k)))

; equal? : identity for mutable objects
(println "equal-arrays-distinct:" (equal? [1 2] [1 2]))
(println "equal-tables-distinct:" (equal? {:a 1} {:a 1}))
(let ((a [1 2]))
  (println "equal-array-self:" (equal? a a)))
(let ((t {:a 1}))
  (println "equal-table-self:" (equal? t t)))

; nil vs () vs #f are all distinct
(println "nil-vs-empty:" (equal? nil '()))
(println "nil-vs-false:" (equal? nil #f))
(println "empty-vs-false:" (equal? '() #f))
(println "nil-vs-nil:" (equal? nil nil))
(println "empty-vs-empty:" (equal? '() '()))

; eq? : interned symbols/keywords; immediates like equal?
(println "eq-symbols:" (eq? 'foo 'foo))
(println "eq-keywords:" (eq? :bar :bar))
(println "eq-ints:" (eq? 7 7))
(println "eq-nil:" (eq? nil nil))
(println "eq-empty:" (eq? '() '()))
(println "eq-bools:" (eq? #t #t))
(let ((a [1]))
  (println "eq-array-self:" (eq? a a)))
(println "eq-arrays-distinct:" (eq? [1] [1]))
(let ((s "same"))
  (println "eq-string-self:" (eq? s s)))
