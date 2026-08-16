;; CPSTAK -- A continuation-passing version of TAK.

(define (cpstak x y z)
  (define (tak-k x y z k)
    (if (not (< y x))
        (k z)
        (tak-k (- x 1) y z
          (lambda (v1)
            (tak-k (- y 1) z x
              (lambda (v2)
                (tak-k (- z 1) x y
                  (lambda (v3) (tak-k v1 v2 v3 k)))))))))
  (tak-k x y z (lambda (a) a)))

(define (benchmark-main)
  (let ((count (bench-read))
        (input1 (bench-read))
        (input2 (bench-read))
        (input3 (bench-read))
        (output (bench-read))
        (name (str "cpstak:" input1 ":" input2 ":" input3 ":" count)))
    (run-ported-benchmark name count
      (lambda ()
        (cpstak (bench-hide count input1)
                (bench-hide count input2)
                (bench-hide count input3)))
      (lambda (result) (= result output)))))
