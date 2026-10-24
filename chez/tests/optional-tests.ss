(import (chezscheme) (otium runtime))

(define checks 0)
(define (check label expected actual)
  (unless (equal? expected actual) (error 'check label expected actual))
  (set! checks (+ checks 1)))

(define (run form) (evaluate form))
(define optional
  (parameterize ([cp0-effort-limit 0])
    (run '(fn (x &optional (y 10) (z (+ x y))) (+ x y z)))))
(check "bounded arity" 14 (procedure-arity-mask optional))
(check "one argument" 22 (optional 1))
(check "two arguments" 8 (optional 1 3))
(check "three arguments" 9 (optional 1 3 5))

(define (exercise f arity)
  (let loop ([n 100000] [result 0])
    (if (= n 0) result
        (loop (- n 1)
              (case arity
                [(1) (f n)]
                [(2) (f n 10)]
                [(3) (f n 10 20)])))))
(define (allocated) (+ (bytes-allocated) (bytes-deallocated)))
(for-each
  (lambda (arity)
    (exercise optional arity)
    (let ([start (allocated)])
      (exercise optional arity)
      (check "no argument lists, with inlining disabled" #t (< (- (allocated) start) 1024))))
  '(1 2 3))

;; Multiple values and captured bindings must survive entry dispatch.
(run '(require chez))
(check "multiple values" '(3 4)
  (call-with-values
    (lambda () (run '((fn (&optional (x 3)) (chez/values x (+ x 1))))))
    list))
(check "captured variable" 17
  (run '(let ((outer 7)) ((fn (&optional (x 10)) (+ x outer))))))

(check "host assignment shares the parameter binding" 9
  (run '((fn (x &optional (read-x (fn () x)))
           (chez/set! x 9)
           (read-x)) 1)))

(printf "~a optional compiler checks passed\n" checks)
