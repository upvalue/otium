(ns demo.counter)

(defonce state (vector 0))

;; Change the increment, then require this namespace with :reload.
(define (tick!)
  (vector-set! state 0 (+ (vector-ref state 0) 1))
  (vector-ref state 0))
