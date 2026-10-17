(import (chezscheme) (otium runtime))

(define checks 0)
(define (check label expected actual)
  (unless (equal? expected actual) (error 'check label expected actual))
  (set! checks (+ checks 1)))
(define (fails? form)
  (guard (c [else #t]) (evaluate form) #f))
(define (output form)
  (let ([port (open-output-string)])
    (parameterize ([current-output-port port]) (evaluate form))
    (get-output-string port)))

(for-each
  (lambda (space)
    (evaluate `(ns ,space))
    (check "chez/ names need (require chez)" #t (fails? '(chez/format "~a" 1)))
    (evaluate '(require chez))
    (for-each
      (lambda (name) (check "host procedure requires prefix" #t (fails? name)))
      '(printf format load load-shared-object environment interaction-environment
        top-level-value set-top-level-value! collect current-time foreign-alloc
        make-message-condition raise))
    (check "native import requires prefix" #t
      (fails? '(import (only (chezscheme) printf))))
    (check "core arithmetic" 42 (evaluate '(+ 20 22)))
    (check "prefixed host procedure" "value: 42"
      (evaluate '(chez/format "value: ~a" 42)))
    (check "output spacing" "hello 42 world\n"
      (output '(println "hello" 42 'world)))
    (check "print and display share behavior" "a 1b 2"
      (output '(begin (print "a" 1) (display "b" 2))))
    (check "empty output calls" "\n\n"
      (output '(begin (print) (display) (println) (newline))))
    (check "output returns false" '(#f #f #f #f)
      (let ([port (open-output-string)])
        (parameterize ([current-output-port port])
          (evaluate '(list (print) (display) (println) (newline))))))
    (check "native display still accepts a port" "native"
      (evaluate '(let ([p (chez/open-output-string)])
                   (chez/display "native" p)
                   (chez/get-output-string p))))
    (check "prefixed native syntax" 'caught
      (evaluate '(chez/guard (c [else 'caught]) (chez/error 'test "failure"))))
    (check "output helpers have documentation" #t
      (> (string-length (evaluate '(describe 'println))) 0)))
  '(user isolated))

(evaluate '(chez/import (only (chezscheme) printf)))
(check "explicit native import" "42" (output '(printf "~a" 42)))
(evaluate '(ns user))
(check "native import stays in its namespace" #t (fails? 'printf))
(evaluate '(def display (fn args 'local)))
(check "core binding can be redefined" 'local (evaluate '(display 1)))
(evaluate '(ns isolated))
(check "core redefinition stays in its namespace" "1" (output '(display 1)))
(check "host binding unaffected by core redefinition" "2"
  (output '(chez/display 2)))
(printf "~a environment checks passed\n" checks)
