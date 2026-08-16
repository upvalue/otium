;; Shared driver for the Otium ports. The generated program defines
;; *benchmark-inputs* and *benchmark-implementation* before loading this file.

(define (bench-read)
  (if (pair? *benchmark-inputs*)
      (let ((value (car *benchmark-inputs*)))
        (set! *benchmark-inputs* (cdr *benchmark-inputs*))
        value)
      (error "benchmark input exhausted")))

(define (bench-make-array n fill)
  (let ((out (array)) (i 0))
    (while (< i n)
      (push! out fill)
      (set! i (+ i 1)))
    out))

(define (bench-hide r x)
  (let ((choices [(lambda (x) x) (lambda (x) x)]))
    ((get choices (if (< r 100) 0 1)) x)))

(define (run-ported-benchmark name count thunk ok?)
  (let ((jps (jiffies-per-second))
        (j0 (current-jiffy))
        (i 0)
        (result #f))
    (while (< i count)
      (set! result (thunk))
      (set! i (+ i 1)))
    (let ((j1 (current-jiffy)))
      (if (ok? result)
          (let ((seconds (inexact (/ (- j1 j0) jps))))
            (println (str "+!CSVLINE!+" *benchmark-implementation* "," name "," seconds))
            seconds)
          (begin
            (println (str "+!CSVLINE!+" *benchmark-implementation* "," name ",INCORRECT"))
            (println "incorrect result:" result)
            #f)))))
