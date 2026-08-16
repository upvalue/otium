;; Integer-keyed table writes and reads exercise native calls from a loop.

(let ((items (table)) (i 0) (sum 0))
  (while (< i 100000)
    (put! items i (* i 2))
    (set! i (+ i 1)))
  (set! i 0)
  (while (< i 100000)
    (set! sum (+ sum (get items i)))
    (set! i (+ i 1)))
  (if (= sum 9999900000)
      nil
      (error "table sum was wrong")))
