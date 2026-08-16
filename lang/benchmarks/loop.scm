;; A tight loop exercises local loads, stores, branches, and arithmetic.

(let ((i 0) (sum 0))
  (while (< i 3000000)
    (set! sum (+ sum i))
    (set! i (+ i 1)))
  (if (= sum 4499998500000)
      nil
      (error "loop sum was wrong")))
