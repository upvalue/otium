; The supported VM startup loads derived core operations from the embedded
; prelude before user code runs.

(println "empty-empty:"
         (empty? nil) (empty? '()) (empty? "") (empty? (buffer))
         (empty? []) (empty? {}))
(println "empty-nonempty:"
         (empty? '(1)) (empty? "x") (empty? (buffer "x"))
         (empty? [1]) (empty? {:a 1}))

(println "get-in-array-path:" (get-in {:a [{:b 7}]} [:a 0 :b]))
(println "get-in-list-path:" (get-in {:a {:b 8}} '(:a :b)))
(println "get-in-default:" (get-in {:a {}} [:a :missing :deeper] 9))
(println "get-in-empty-path:" (get-in 42 []))
(println "get-in-nil-path:" (get-in 42 nil))
; String indexing allocates before the iterator advances to the final key.
(println "get-in-allocating-path:" (get-in {:s "ab"} [:s 0 0]))
(println "get-in-improper:"
  (try (get-in {:a 1} '(:a . :tail))
    (catch (error? e) (condition-message e))))
