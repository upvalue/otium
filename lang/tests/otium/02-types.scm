; Types and values: truthiness (2.1), nil rules (2.2), type keywords.

; truthiness: only nil and #f are falsy
(println "falsy-nil:" (if nil "wrong" "ok"))
(println "falsy-false:" (if #f "wrong" "ok"))
(println "truthy-empty-list:" (if '() "ok" "wrong"))
(println "truthy-zero:" (if 0 "ok" "wrong"))
(println "truthy-zero-float:" (if 0.0 "ok" "wrong"))
(println "truthy-empty-string:" (if "" "ok" "wrong"))
(println "truthy-empty-array:" (if [] "ok" "wrong"))
(println "not-nil:" (not nil))
(println "not-empty-list:" (not '()))

; nil: missing lookups yield nil
(println "get-missing:" (get {} :k))
(println "get-default:" (get {} :k 99))
(println "get-on-nil:" (get nil :k))
(println "get-in-nil-chain:" (get-in {:a {:b 1}} [:a :missing :deeper]))
(println "get-in-hit:" (get-in {:a {:b 1}} [:a :b]))

; storing nil deletes
(let ((t {:a 1 :b 2}))
  (put! t :a nil)
  (println "put-nil-deletes:" (count t))
  (println "put-nil-gone:" (contains? t :a)))
(println "ctor-nil-deletes:" (count {:a nil}))

; nil as empty sequence
(println "count-nil:" (count nil))
(println "first-nil:" (first nil))
(println "rest-nil:" (rest nil))
(println "nth-nil:" (nth nil 0))
(println "last-nil:" (last nil))
(println "empty-nil:" (empty? nil))

; type keywords
(println "type-int:" (type 1))
(println "type-float:" (type 1.5))
(println "type-nil:" (type nil))
(println "type-null:" (type '()))
(println "type-bool:" (type #t))
(println "type-symbol:" (type 'foo))
(println "type-keyword:" (type :foo))
(println "type-string:" (type "s"))
(println "type-pair:" (type (cons 1 2)))
(println "type-array:" (type [1]))
(println "type-table:" (type {}))
(println "type-function:" (type type))

; type predicates
(println "nil?-nil:" (nil? nil))
(println "nil?-empty:" (nil? '()))
(println "null?-empty:" (null? '()))
(println "null?-nil:" (null? nil))
(println "number?-both:" (number? 1) (number? 1.5) (number? "1"))
(println "list?-proper:" (list? '(1 2)) (list? '()))
