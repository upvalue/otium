;; NQUEENS -- Compute the number of solutions to the n-queens problem.

(define (nqueens n)
  (define (iota1 n)
    (define (loop i result)
      (if (= i 0) result (loop (- i 1) (cons i result))))
    (loop n '()))

  (define (ok? row distance placed)
    (if (null? placed)
        #t
        (and (not (= (car placed) (+ row distance)))
             (not (= (car placed) (- row distance)))
             (ok? row (+ distance 1) (cdr placed)))))

  (define (search x y placed)
    (if (null? x)
        (if (null? y) 1 0)
        (+ (if (ok? (car x) 1 placed)
               (search (append (cdr x) y) '() (cons (car x) placed))
               0)
           (search (cdr x) (cons (car x) y) placed))))

  (search (iota1 n) '() '()))

(define (benchmark-main)
  (let ((count (bench-read))
        (input (bench-read))
        (output (bench-read))
        (name (str "nqueens:" input ":" count)))
    (run-ported-benchmark name count
      (lambda () (nqueens (bench-hide count input)))
      (lambda (result) (= result output)))))
