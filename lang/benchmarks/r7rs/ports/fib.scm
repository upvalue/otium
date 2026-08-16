;; FIB -- A classic benchmark, computes fib(n) inefficiently.

(define (fib n)
  (if (< n 2)
      n
      (+ (fib (- n 1)) (fib (- n 2)))))

(define (benchmark-main)
  (let ((count (bench-read))
        (input (bench-read))
        (output (bench-read))
        (name (str "fib:" input ":" count)))
    (run-ported-benchmark name count
      (lambda () (fib (bench-hide count input)))
      (lambda (result) (= result output)))))
