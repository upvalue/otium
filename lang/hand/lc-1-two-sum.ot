;; leetcode-1-two-sum.scm - https://leetcode.com/problems/two-sum/

(define (two-sum nums target)
    (define seent {})
    (define result #f)
    (dotimes (i (length nums))
        ;(println seent)
        ;(println i)
        (unless result)
            (let ((n (nums i)))
                (when (seent (- target n))
                    (begin
                        ;(println "crash1")
                        (set! result (array i ((seent (- target n)) 1))))
                        ;(println "crash2")
                    )
                (put! seent n (array (- target n) i))))

    result)


(println (two-sum [2 7 11 15] 9))
(println (two-sum [3 2 4] 6))
(println (two-sum [3 3] 6))
(println (two-sum [3 14 -2 8 11 7 4 19 0 6 13 -5] 15))
