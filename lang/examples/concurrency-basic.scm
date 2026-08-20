; Cooperative processes run in round-robin reduction slices.

(define (count label n)
  (let loop ((i 1))
    (if (<= i n)
        (begin
          (println label i (self))
          (yield)
          (loop (+ i 1))))))

(println "root" (self))
(spawn count "alpha" 3)
(spawn count "beta" 3)
