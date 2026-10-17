(import (chezscheme) (otium runtime) (prefix (otium generic-core) gm:))

;; All strategies consume the same changing inputs and return the same checksum.
;; Construction, compilation, registration, and collection precede each timing.
(define calls (if (> (length (command-line)) 1)
                  (string->number (cadr (command-line))) 100000))
(define repeats 5)
(define samples 1024)
(unless (and (integer? calls) (> calls 0)) (error 'benchmark "positive call count required"))
(define (run form) (evaluate form))
(run '(require otium.generic :as g))
(define env (environment '(chezscheme) '(prefix (otium generic-core) gm:)))
(define (now)
  (let ([t (current-time 'time-monotonic)])
    (+ (* (time-second t) 1000000000) (time-nanosecond t))))
(define (median xs) (list-ref (sort < xs) (div (length xs) 2)))
(define random-state 20261016)
(define (next bound)
  (set! random-state (mod (+ (* random-state 1664525) 1013904223) 4294967296))
  (mod (div random-state 65536) bound))

(define (trial nx ny binary?)
  (define (name prefix i) (string->symbol (format "~a~a" prefix i)))
  (define xs (map (lambda (i) (name "Widget" i)) (iota nx)))
  (define ys (map (lambda (i) (name "Event" i)) (iota ny)))
  (define declarations
    (begin
      (for-each (lambda (n) (run `(define-type ,n))) (append xs ys))
      (run '(define-generic (operation x y)))
      (run '(reset-generic! (operation x y)))
      (run '(define-method (operation x y) -1))
      (when binary?
        (for-each (lambda (n) (run `(define-method (operation (x ,n) y) -2))) xs))))
  (define generic (run 'operation))
  (define xt (list->vector (map run xs)))
  (define yt (list->vector (map run ys)))
  (define methods (make-vector (* nx ny)))
  (define clauses '())
  (define registration
    (begin
      (do ([a 0 (+ a 1)]) ((= a nx))
        (do ([b 0 (+ b 1)]) ((= b ny))
          (let* ([offset (+ (* a ny) b)]
                 [body `(+ (gm:field-ref x 'payload) (gm:field-ref y 'payload) ,offset)]
                 [impl (eval `(fn (x y) ,body) env)])
            (vector-set! methods offset impl)
            (gm:add-method! generic
              (list (vector-ref xt a) (and binary? (vector-ref yt b))) impl)
            (set! clauses
              (cons `[(and (eq? tx (vector-ref xt ,a))
                           ,(if binary? `(eq? ty (vector-ref yt ,b)) #t))
                      ,body] clauses)))))
      (set! clauses (reverse clauses))))
  ;; Compile each cond with its descriptor vectors captured as lexical arguments.
  (define (cond-procedure read-x read-y prefix?)
    ((eval `(fn (xt yt)
              (fn (x y)
                (let ([tx ,read-x] [ty ,read-y])
                  (cond
                    ,@(if prefix?
                          (map (fn (clause)
                                 (list (car clause)
                                   `(+ (vector-ref x 1) (vector-ref y 1)
                                       ,(cadddr (cadr clause))))) clauses)
                          clauses)
                    [else -1])))) env) xt yt))
  (define by-type (cond-procedure '(gm:dispatch-type x) '(gm:dispatch-type y) #f))
  (define by-field (cond-procedure '(gm:field-ref x 'kind) '(gm:field-ref y 'kind) #f))
  (define by-prefix (cond-procedure '(vector-ref x 0) '(vector-ref y 0) #t))
  (define lefts (make-vector samples))
  (define rights (make-vector samples))
  (define prefix-lefts (make-vector samples))
  (define prefix-rights (make-vector samples))
  (define selected (make-vector samples))
  (define inputs-ready
    (do ([i 0 (+ i 1)]) ((= i samples))
      (let* ([a (next nx)] [b (next ny)] [v (next 10000)] [w (next 10000)]
             [tx (vector-ref xt a)] [ty (vector-ref yt b)])
        (vector-set! lefts i (gm:make tx 'kind tx 'payload v))
        (vector-set! rights i (gm:make ty 'kind ty 'payload w))
        (vector-set! prefix-lefts i (vector tx v))
        (vector-set! prefix-rights i (vector ty w))
        (vector-set! selected i (vector-ref methods (+ (* a ny) b))))))
  (define (loop operation lefts rights count)
    (let iterate ([n count] [checksum 0])
      (if (= n 0) checksum
          (let ([i (bitwise-and n (- samples 1))])
            (iterate (- n 1)
              (+ checksum (operation i (vector-ref lefts i) (vector-ref rights i))))))))
  (define strategies
    (list
      (list 'preselected-method (lambda (i x y) ((vector-ref selected i) x y)) lefts rights)
      (list 'multimethod-scan (lambda (i x y) (generic x y)) lefts rights)
      (list 'cond-descriptor (lambda (i x y) (by-type x y)) lefts rights)
      (list 'cond-map-field (lambda (i x y) (by-field x y)) lefts rights)
      (list 'cond-vector-prefix (lambda (i x y) (by-prefix x y)) prefix-lefts prefix-rights)))
  (define expected (loop (cadar strategies) lefts rights calls))
  (for-each
    (lambda (entry)
      (let ([operation (cadr entry)] [ls (caddr entry)] [rs (cadddr entry)]
            [times '()] [allocations '()])
        (loop operation ls rs (min calls 10000))
        (do ([r 0 (+ r 1)]) ((= r repeats))
          (collect)
          (let* ([before (statistics)] [start (now)]
                 [checksum (loop operation ls rs calls)]
                 [elapsed (- (now) start)] [allocated (sstats-bytes (sstats-difference (statistics) before))])
            (unless (= checksum expected) (error 'benchmark "checksum mismatch" (car entry)))
            (set! times (cons (/ (inexact elapsed) calls) times))
            (set! allocations (cons (/ (inexact allocated) calls) allocations))))
        (printf "~a,~a,~a,~a,~a,~a,~a,~a,~a\n"
          (if binary? 'two 'one) (* nx ny) (length (gm:generic-methods generic))
          (car entry) calls (median times) (apply min times) (median allocations) expected)))
    strategies))

(printf "# Chez ~a; machine ~a; optimize-level ~a; repeats ~a; samples ~a\n"
  (scheme-version) (machine-type) (optimize-level) repeats samples)
(display "dispatch_positions,exact_methods,total_methods,strategy,calls,median_ns_per_call,min_ns_per_call,median_allocated_bytes_per_call,checksum\n")
(for-each (lambda (shape) (apply trial shape))
  '((4 1 #f) (16 1 #f) (64 1 #f) (2 2 #t) (4 4 #t) (8 8 #t)))
