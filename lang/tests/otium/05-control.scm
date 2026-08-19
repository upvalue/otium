; Control flow: if, cond, and/or, sequential let, while, begin.

; if
(println "if-true:" (if #t 1 2))
(println "if-false:" (if #f 1 2))
(println "if-no-else:" (if #f 1))
(println "if-nil-test:" (if nil 1 2))

; cond
(println "cond-first-truthy:" (cond (#f 1) (#t 2) (else 3)))
(println "cond-one-element:" (cond (#f 1) (42) (else 3)))
(println "cond-no-clause:" (cond (#f 1) (#f 2)))
(println "cond-else:" (cond (#f 1) (else 3)))
(println "cond-body-last:" (cond (#t 1 2 3)))
(println "cond-truthy-value:" (cond (nil 1) ("hit" 2)))

; and / or return values
(println "and-empty:" (and))
(println "or-empty:" (or))
(println "and-all-truthy:" (and 1 2 3))
(println "and-first-falsy:" (and 1 nil 3))
(println "and-false:" (and 1 #f 3))
(println "or-first-truthy:" (or nil #f 7 8))
(println "or-all-falsy:" (or nil #f))
(println "or-last-nil:" (or #f nil))
(println "and-truthy-empty-list:" (and '() 5))

; let is sequential (let*)
(println "let-seq:" (let ((a 1) (b (+ a 1)) (c (* b 3))) (list a b c)))
(println "let-shadow:" (let ((x 1)) (let ((x 2) (y x)) (list x y))))
(println "let-empty-body:" (let ((x 1))))

; named let is a local recursive function; its initializers use the outer scope
(println "named-let-zero:" (let loop () #t))
(println "named-let-empty-body:" (let loop ()))
(println "named-let-sum:"
         (let sum ((n 10) (acc 0))
           (if (= n 0) acc (sum (- n 1) (+ acc n)))))
(println "named-let-initializers:"
         (let ((x 10))
           (let loop ((x 1) (outer-x x)) (list x outer-x))))
(println "named-let-scope:"
         (let ((walk 'outer))
           (list (let walk ((n 2))
                   (if (= n 0) 'inner (walk (- n 1))))
                 walk)))
(println "named-let-tail:"
         (let count ((n 50000) (acc 0))
           (if (= n 0) acc (count (- n 1) (+ acc n)))))

; while returns nil, loops while truthy
(let ((i 0) (acc 0))
  (while (< i 5)
    (set! acc (+ acc i))
    (set! i (+ i 1)))
  (println "while-sum:" acc)
  (println "while-i:" i))
(println "while-value:" (while #f 1))

; begin
(println "begin-last:" (begin 1 2 3))
(println "begin-empty:" (begin))
(println "do-alias:" (do 1 2))
