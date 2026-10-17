;; FIB -- A classic benchmark, computes fib(n) inefficiently.

(def (fib n)
  (if (< n 2)
      n
      (+ (fib (- n 1)) (fib (- n 2)))))

(def (benchmark-main)
  (let ((count (bench-read))
        (input (bench-read))
        (output (bench-read))
        (name (str "fib:" input ":" count)))
    (run-ported-benchmark name count
      (fn () (fib (bench-hide count input)))
      (fn (result) (= result output)))))
