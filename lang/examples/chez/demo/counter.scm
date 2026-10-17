(ns demo.counter)

(defonce state [0])

;; Change the increment, then require this namespace with :reload.
(def (tick!)
  (put! state 0 (+ (get state 0) 1))
  (get state 0))
