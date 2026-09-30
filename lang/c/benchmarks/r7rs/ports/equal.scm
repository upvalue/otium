;; EQUAL -- Exercise structural equality on several acyclic shapes.
;; The upstream cyclic-list case is defined but disabled, so it is omitted here.

(define (make-test-list1 n x)
  (if (zero? n)
      '()
      (cons x (make-test-list1 (- n 1) x))))

(define (make-test-tree1 n)
  (if (zero? n)
      '()
      (make-test-list1 n (make-test-tree1 (- n 1)))))

(define (make-test-list2 n thunk)
  (if (zero? n)
      '()
      (cons (thunk) (make-test-list2 (- n 1) thunk))))

(define (make-test-tree2 n)
  (if (zero? n)
      '()
      (make-test-list2 n (lambda () (make-test-tree2 (- n 1))))))

(define (make-test-tree5 n)
  (if (zero? n)
      '()
      (cons (make-test-tree5 (- n 1)) 'a)))

(define (iterate n thunk)
  (cond
    ((= n 1) (thunk))
    ((> n 1) (thunk) (iterate (- n 1) thunk))
    (else #f)))

(define (equality-benchmark1 n)
  (let ((x (make-test-tree1 n))
        (y (make-test-tree1 n)))
    (iterate n (bench-hide n (lambda () (equal? x y))))))

(define (equality-benchmark2 n)
  (let ((x (make-test-tree2 n))
        (y (make-test-tree2 n)))
    (iterate n (bench-hide n (lambda () (equal? x y))))))

(define (equality-benchmark3 n)
  (let ((x (bench-make-array n 'a))
        (y (bench-make-array n 'a)))
    (iterate n (bench-hide n (lambda () (equal? x y))))))

(define (equality-benchmark4 n)
  (let ((x (array->list (bench-make-array n (make-test-tree2 3))))
        (y (array->list (bench-make-array n (make-test-tree2 3)))))
    (iterate n (bench-hide n (lambda () (equal? x y))))))

(define (equality-benchmark5 n iterations)
  (let ((x (make-test-tree5 n))
        (y (make-test-tree5 n)))
    (iterate iterations (bench-hide n (lambda () (equal? x y))))))

(define (equality-benchmarks n1 n2 n3 n4 n5)
  (and (equality-benchmark1 n1)
       (equality-benchmark2 n2)
       (equality-benchmark3 n3)
       (equality-benchmark4 n4)
       (equality-benchmark5 n5 100)))

(define (benchmark-main)
  (let ((n0 (bench-read))
        (n1 (bench-read))
        (n2 (bench-read))
        (n3 (bench-read))
        (n4 (bench-read))
        (n5 (bench-read))
        (output (bench-read)))
    (let ((name (str "equal:" n0 ":" n1 ":" n2 ":" n3 ":" n4 ":" n5)))
      (run-ported-benchmark name 1
        (lambda ()
          (equality-benchmarks (bench-hide n0 n1)
                               (bench-hide n0 n2)
                               (bench-hide n0 n3)
                               (bench-hide n0 n4)
                               (bench-hide n0 n5)))
        (lambda (result) (eq? result output))))))
