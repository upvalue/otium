;; ACK -- One of the Kernighan and Van Wyk benchmarks.

(define (ack m n)
  (cond ((= m 0) (+ n 1))
        ((= n 0) (ack (- m 1) 1))
        (else (ack (- m 1) (ack m (- n 1))))))

(define (benchmark-main)
  (let ((count (bench-read))
        (input1 (bench-read))
        (input2 (bench-read))
        (output (bench-read))
        (name (str "ack:" input1 ":" input2 ":" count)))
    (run-ported-benchmark name count
      (lambda () (ack (bench-hide count input1) (bench-hide count input2)))
      (lambda (result) (= result output)))))
