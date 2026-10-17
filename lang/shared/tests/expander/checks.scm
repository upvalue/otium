;; Expander behaviour shared by the C and Chez runtimes.
(def checks 0)
(def (check label expected actual)
  (if (equal? expected actual)
      (set! checks (+ checks 1))
      (begin (println "FAIL" label "expected" expected "got" actual) (exit 1))))
(def (fails? form)
  (try (begin (eval form) #f)
    (catch (error? e) #t)))

(defmacro twice (x)
  (let ((t (gensym "t")))
    `(let ((,t ,x)) (+ ,t ,t))))
(defmacro install (name value) `(def ,name ,value))
(defmacro outer (x) `(inner ,x))
(defmacro inner (x) `(+ ,x 1))
(defmacro give-keyword () :k)
(defmacro give-nil () nil)

;; let binds sequentially; named let initializers see the enclosing scope.
(check "sequential let" 3 (let ((a 1) (b (+ a 1)) (a (+ b 1))) a))
(check "named let initializers" 10 (let ((x 10)) (let loop ((x 1) (outer-x x)) outer-x)))
(check "let body shadows macro" 3 (let ((twice (fn (x) x))) (twice 3)))
(check "initializer after shadowing binding" 3
  (let ((twice (fn (x) x)) (result (twice 3))) result))

;; Bodies: definitions anywhere, all visible throughout, macros may produce them.
(def (forward)
  (def (g) (h))
  (def seen (list 1))
  (def (h) 5)
  (g))
(check "forward reference between body definitions" 5 (forward))
(def (interleaved)
  (def a 1)
  (def first a)
  (set! a 2)
  (def second a)
  (list first second))
(check "definitions follow expressions" '(1 2) (interleaved))
(check "macro-produced body definition" 7 (let () (install x 7) x))
(check "body begin is spliced" 3 (let () (begin (def (f) (g)) (def (g) 3)) (f)))
(check "body ending in a definition" #t (nil? ((fn () (def x 1)))))
(check "empty body" #t (nil? ((fn ()))))

;; Parameter lists.
(check "ampersand rest" '(2 3) ((fn (a & rest) rest) 1 2 3))
(check "array spelling" '(1 (2)) ((fn [a & rest] (list a rest)) 1 2))
(check "dotted rest" '(2 3) ((fn (a . rest) rest) 1 2 3))
(check "rest symbol" '(1 2) ((fn args args) 1 2))
(check "duplicate parameter" #t (fails? '(fn (x x) x)))

;; Forms that no longer exist, and reserved names.
(check "lambda is not a form" #t (fails? '((lambda (x) x) 1)))
(check "define is not a form" #t (fails? '(define y 1)))
(check "do is not a form" #t (fails? '(do 1 2)))
(check "core names cannot be rebound" #t (fails? '(let ((if 1)) if)))
(check "core names cannot be parameters" #t (fails? '(fn (quote) quote)))
(check "core names cannot be macros" #t (fails? '(defmacro begin () 1)))
(check "definition in expression context" #t (fails? '(if #t (def z 1) 2)))
(check "private definition in a body" #t (fails? '(fn () (def- z 1))))
(check "malformed let" #t (fails? '(let ((x 1 2)) x)))

;; Expansion details.
(check "macro output keyword" :k (give-keyword))
(check "macro output nil" #t (nil? (give-nil)))
(check "keyword argument" '(:a 1) (list :a 1))
(check "macroexpand-1" '(inner 2) (macroexpand-1 '(outer 2)))
(check "macroexpand" '(+ 2 1) (macroexpand '(outer 2)))
(check "macroexpand stops at core forms" '(if 1 2) (macroexpand '(if 1 2)))
(check "quasiquote unquote expands" '(a 42) `(a ,(twice 21)))
(check "nested quasiquote stays data" '(quasiquote ((unquote (twice 3))))
  `(quasiquote ((unquote (twice 3)))))
(check "catch body definitions" 3
  (try (error "boom") (catch (error? e) (def x 1) (def y (+ x 2)) y)))
(check "while body expands" 4
  (let ((n 0)) (while (< n 4) (set! n (+ n (twice 0) 1))) n))
(check "cond" 2 (cond (#f 1) ((twice 1) 2) (else 3)))
(check "set! value expands" 6 (let ((x 0)) (set! x (twice 3)) x))

;; Local symbol macros.
(def cell {:x 1 :y 2})
(check "symbol macro reads" 3
  (symbol-macrolet ((x (get cell :x)) (y (get cell :y))) (+ x y)))
(check "symbol macro writes" 11
  (symbol-macrolet ((x (get cell :x) (v (put! cell :x v))))
    (set! x (+ x 10))
    (get cell :x)))
(check "read-only symbol macro rejects set!" #t
  (fails? '(symbol-macrolet ((x 1)) (set! x 2))))
(check "inner binding shadows symbol macro" 5
  (symbol-macrolet ((x (error "expanded"))) (let ((x 5)) x)))
(check "inner set! on shadowing binding" 6
  (symbol-macrolet ((x (error "expanded"))) (let ((x 5)) (set! x 6) x)))
(check "symbol macro in operator position" 4
  (symbol-macrolet ((f (fn (n) (* n 2)))) (f 2)))
(check "expansion is not re-expanded as itself" 1
  (let ((x 1)) (symbol-macrolet ((x x)) x)))
(check "body definitions" 3
  (symbol-macrolet ((x 1)) (def y (+ x 1)) (+ x y)))
(check "symbol macro inside quasiquote" '(a 1)
  (symbol-macrolet ((x 1)) `(a ,x)))
(check "quoted name is data" 'x (symbol-macrolet ((x 1)) 'x))
(defmacro with-keys (object names . body)
  (let ((o (gensym "object")))
    `(let ((,o ,object))
       (symbol-macrolet
           ,(map (fn (n)
                   (let ((k (keyword (symbol->string n))))
                     (list n (list 'get o k) (list 'v (list 'put! o k 'v)))))
                 names)
         ,@body))))
(def (move! p)
  (with-keys p (x y)
    (set! x (+ x 1))
    (set! y (* y 10))
    (list x y)))
(check "with-keys macro" '(12 20) (move! cell))
(check "with-keys wrote through" 12 (get cell :x))

;; Top level.
(begin
  (defmacro later (x) `(+ ,x 1))
  (def later-result (later 41)))
(check "top-level begin runs in order" 42 later-result)
(defonce once 1)
(defonce once 2)
(check "defonce keeps the first value" 1 once)
(defparam depth 0)
(check "with-params binds sequentially" '(1 2)
  (with-params ((depth 1) (depth (+ (depth) 1))) (list 1 (depth))))

(println "expander checks passed" checks)
