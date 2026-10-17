;; DERIV -- Symbolic derivation.

(define (deriv expression)
  (cond ((not (pair? expression))
         (if (eq? expression 'x) 1 0))
        ((eq? (car expression) '+)
         (cons '+ (map deriv (cdr expression))))
        ((eq? (car expression) '-)
         (cons '- (map deriv (cdr expression))))
        ((eq? (car expression) '*)
         (list '* expression
               (cons '+
                     (map (lambda (item) (list '/ (deriv item) item))
                          (cdr expression)))))
        ((eq? (car expression) '/)
         (list '-
               (list '/ (deriv (second expression)) (third expression))
               (list '/ (second expression)
                     (list '* (third expression) (third expression)
                           (deriv (third expression))))))
        (else (error "no derivation method available"))))

(define (benchmark-main)
  (let ((count (bench-read))
        (input (bench-read))
        (output (bench-read))
        (name (str "deriv:" count)))
    (run-ported-benchmark name count
      (lambda () (deriv (bench-hide count input)))
      (lambda (result) (equal? result output)))))
