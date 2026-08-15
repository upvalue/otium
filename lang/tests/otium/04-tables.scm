; Table insertion order per spec 2.3: update keeps position,
; delete+reinsert moves to end, merge and {} are left-to-right.

; base insertion order
(let ((t {:a 1 :b 2 :c 3}))
  (println "keys-order:" (keys t))
  (println "values-order:" (values t)))

; updating an existing key keeps its position
(let ((t {:a 1 :b 2 :c 3}))
  (put! t :a 9)
  (println "update-keys:" (keys t))
  (println "update-values:" (values t)))

; delete then reinsert moves the key to the end
(let ((t {:a 1 :b 2 :c 3}))
  (put! t :a nil)
  (put! t :a 4)
  (println "reinsert-keys:" (keys t))
  (println "reinsert-values:" (values t)))

; {} constructor: duplicate key is an update, keeps first position
(let ((t {:x 1 :y 2 :x 3}))
  (println "ctor-dup-keys:" (keys t))
  (println "ctor-dup-values:" (values t)))

; merge processes left to right; result order is first-seen order
(let ((m (merge {:a 1 :b 2} {:c 3 :a 9})))
  (println "merge-keys:" (keys m))
  (println "merge-values:" (values m)))

; merge skips nil arguments; nil values delete
(let ((m (merge {:a 1 :b 2} nil {:a nil :c 3})))
  (println "merge-nil-keys:" (keys m))
  (println "merge-nil-values:" (values m)))

; merge makes a new table (inputs untouched)
(let ((t1 {:a 1}))
  (merge t1 {:b 2})
  (println "merge-fresh:" (count t1)))

; keys/values of nil
(println "keys-nil:" (keys nil))
(println "values-nil:" (values nil))
