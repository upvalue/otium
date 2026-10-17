;; Shared driver for the Otium ports. The generated program defines
;; *benchmark-inputs* and *benchmark-implementation* before loading this file.

(def (bench-read)
  (if (pair? *benchmark-inputs*)
      (let ((value (car *benchmark-inputs*)))
        (set! *benchmark-inputs* (cdr *benchmark-inputs*))
        value)
      (error "benchmark input exhausted")))

(def (bench-make-array n fill)
  (let ((out (array)) (i 0))
    (while (< i n)
      (push! out fill)
      (set! i (+ i 1)))
    out))

(def (bench-hide r x)
  (let ((choices [(fn (x) x) (fn (x) x)]))
    ((get choices (if (< r 100) 0 1)) x)))

(def (run-ported-benchmark name count thunk ok?)
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
