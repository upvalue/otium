; Proper tail calls (spec 3.5): these run 100000+ iterations and must
; complete in constant stack. Magnitudes stay within 31 bits.

; mutual recursion through if branches (tail positions)
(define (my-even? n) (if (= n 0) #t (my-odd? (- n 1))))
(define (my-odd? n) (if (= n 0) #f (my-even? (- n 1))))
(println "mutual-even:" (my-even? 100000))
(println "mutual-odd:" (my-odd? 100001))

; self tail recursion with accumulator
(define (count-up n acc)
  (if (= n 0) acc (count-up (- n 1) (+ acc 1))))
(println "count-up:" (count-up 200000 0))

; tail call through cond clause body
(define (cond-loop n)
  (cond ((= n 0) 'done)
        (else (cond-loop (- n 1)))))
(println "cond-tail:" (cond-loop 100000))

; tail call as last form of and/or
(define (and-loop n)
  (and #t (if (= n 0) 'and-done (and-loop (- n 1)))))
(println "and-tail:" (and-loop 100000))
(define (or-loop n)
  (or #f (if (= n 0) 'or-done (or-loop (- n 1)))))
(println "or-tail:" (or-loop 100000))

; tail call as last body form of let and begin
(define (let-loop n)
  (let ((m (- n 1)))
    (if (= n 0) 'let-done (let-loop m))))
(println "let-tail:" (let-loop 100000))
(define (begin-loop n)
  (begin 'ignored (if (= n 0) 'begin-done (begin-loop (- n 1)))))
(println "begin-tail:" (begin-loop 100000))
