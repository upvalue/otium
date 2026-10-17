;; TAK -- A vanilla version of the TAKeuchi function.

(define (tak x y z)
  (if (not (< y x))
      z
      (tak (tak (- x 1) y z)
           (tak (- y 1) z x)
           (tak (- z 1) x y))))

(define (benchmark-main)
  (let ((count (bench-read))
        (input1 (bench-read))
        (input2 (bench-read))
        (input3 (bench-read))
        (output (bench-read))
        (name (str "tak:" input1 ":" input2 ":" input3 ":" count)))
    (run-ported-benchmark name count
      (lambda ()
        (tak (bench-hide count input1)
             (bench-hide count input2)
             (bench-hide count input3)))
      (lambda (result) (= result output)))))
