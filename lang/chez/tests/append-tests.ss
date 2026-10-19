(import (chezscheme) (otium runtime))

(define checks 0)
(define (check label expected actual)
  (unless (equal? expected actual) (error 'check label expected actual))
  (set! checks (+ checks 1)))
(define (run form) (evaluate form))
(define (fails? form)
  (guard (c [else #t]) (run form) #f))
(run '(require chez))

(check "no arguments" '() (run '(append)))
(check "one argument" '(1 2) (run '(append '(1 2))))
(check "two arguments" '(1 2 3 4) (run '(append '(1 2) '(3 4))))
(check "many arguments" '(1 2 3 4)
  (run '(append '() '(1) '(2 3) '() '(4))))
(check "empty arguments" '() (run '(append '() '() '())))
(check "one argument keeps identity" #t
  (run '(let ([xs (list 1 2)]) (eq? xs (append xs)))))
(check "empty prefix keeps tail identity" #t
  (run '(let ([xs (list 1 2)]) (eq? xs (append '() xs)))))
(check "prefix copied and final tail shared" '(#f #t (1 2))
  (run '(let ([xs (list 1 2)] [ys (list 3 4)] [out (append xs ys)])
          (list (eq? out xs) (eq? (cddr out) ys) xs))))
(check "variadic final tail shared" #t
  (run '(let ([xs (list 3 4)]) (eq? (cddr (append '(1) '(2) xs)) xs))))
(check "nested improper list is an element" '((1 . 2) 3)
  (run '(append '((1 . 2)) '(3))))

;; Check every position, including the final argument that native append
;; accepts as an arbitrary tail. Nil is not an empty list here.
(for-each
  (lambda (bad)
    (for-each
      (lambda (form) (check "reject non-list argument" #t (fails? form)))
      `((append ,bad)
        (append ,bad '(1))
        (append '(1) ,bad)
        (append ,bad '(1) '(2))
        (append '(1) ,bad '(2))
        (append '(1) '(2) ,bad))))
  '(nil #f 7 '(1 . 2) (array 1) (chez/vector 1)))

(for-each
  (lambda (application)
    (check "reject circular list" #t
      (fails? `(let ([xs (list 1)])
                 (chez/set-cdr! xs xs)
                 ,application))))
  '((append xs) (append xs '()) (append '() xs)
    (append xs '() '()) (append '() xs '()) (append '() '() xs)))
(check "apply uses the same validation" #t
  (fails? '(apply append (list '(1) 2))))
(check "apply accepts proper lists" '(1 2 3)
  (run '(apply append (list '(1) '(2) '(3)))))
(check "helper names can be shadowed" '(1 2 3)
  (run '(let ([for-each 0] [apply 0]) (append '(1) '(2) '(3)))))

(run '(def saved-append append))
(run '(def (builtin-join xs) (append xs '(2))))
(run '(def append saved-append))
(run '(def (join xs) (append xs '(2))))
(check "caller before replacement" '(1 2) (run '(join '(1))))
(run '(def append (fn (xs ys) 'replacement)))
(check "previously compiled builtin reference stays bound" '(1 2) (run '(builtin-join '(1))))
(check "existing caller follows replacement" 'replacement (run '(join '(1))))
(check "saved append remains callable" '(1 2) (run '(saved-append '(1) '(2))))
(run '(def append saved-append))
(check "existing caller follows restoration" '(1 2) (run '(join '(1))))

(printf "~a append checks passed\n" checks)
