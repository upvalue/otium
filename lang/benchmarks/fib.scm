;; Naive recursive Fibonacci exercises function calls and arithmetic.

(define (fib n)
  (if (< n 2)
      n
      (+ (fib (- n 1)) (fib (- n 2)))))

(if (= (fib 35) 832040)
    nil
    (error "fib(30) returned the wrong result"))
