;; TAKL -- The TAKeuchi function using lists as counters.

(define (mas x y z)
  (if (not (shorter? y x))
      z
      (mas (mas (cdr x) y z)
           (mas (cdr y) z x)
           (mas (cdr z) x y))))

(define (shorter? x y)
  (and (not (null? y))
       (or (null? x) (shorter? (cdr x) (cdr y)))))

(define (benchmark-main)
  (let ((count (bench-read))
        (input1 (bench-read))
        (input2 (bench-read))
        (input3 (bench-read))
        (output (bench-read))
        (name (str "takl:" (length input1) ":" (length input2) ":"
                   (length input3) ":" count)))
    (run-ported-benchmark name count
      (lambda ()
        (mas (bench-hide count input1)
             (bench-hide count input2)
             (bench-hide count input3)))
      (lambda (result) (= (length result) output)))))
