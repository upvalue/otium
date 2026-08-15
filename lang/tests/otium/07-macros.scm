; Macros: prelude macros, defmacro + gensym + quasiquote, expansion
; order (defined mid-file, usable after), macroexpand-1.

; prelude macros
(println "when-true:" (when #t 1 2))
(println "when-false:" (when #f 1))
(println "unless-false:" (unless #f 3))
(println "unless-true:" (unless #t 3))
(println "thread-first:" (-> 5 (+ 1) (* 2)))
(println "thread-first-bare:" (-> 5 inc inc))
(println "thread-last:" (->> 5 (- 1)))
(println "thread-last-list:" (->> [1 2 3] (map inc) (reduce + 0)))
(dotimes (i 3) (println "dotimes:" i))
(defn add3 (a b c) (+ a b c))
(println "defn:" (add3 1 2 3))

; defmacro with quasiquote and gensym; defined here, used after
(defmacro twice (e)
  (let ((g (gensym)))
    `(let ((,g ,e)) (+ ,g ,g))))
(println "twice:" (twice 21))

; unhygienic capture works when done on purpose: the expansion below
; refers to a `base` that exists at the expansion site
(defmacro add-base (e) `(+ base ,e))
(let ((base 100))
  (println "capture:" (add-base 5)))

; gensym does not collide with a same-prefixed source symbol
(defmacro double-shadowing (e)
  (let ((tmp (gensym "x")))
    `(let ((,tmp ,e)) (* 2 ,tmp))))
(let ((x 7))
  (println "gensym-no-collide:" (double-shadowing x)))

; macro body may call functions defined before it
(define (expander-helper x) (list '+ x 1))
(defmacro inc-form (x) (expander-helper x))
(println "macro-calls-fn:" (inc-form 41))

; quasiquote with splicing inside a macro
(defmacro sum-of es `(+ ,@es))
(println "splice:" (sum-of 1 2 3 4))

; macroexpand-1: one step, no descent
(defmacro m1 (x) `(+ ,x 1))
(println "expand-1:" (macroexpand-1 '(m1 2)))
(println "expand-1-nonmacro:" (macroexpand-1 '(+ 1 2)))
(println "expand-1-atom:" (macroexpand-1 42))
(defmacro m2 (x) `(m1 ,x))
(println "expand-1-once:" (macroexpand-1 '(m2 5)))
(println "expand-full:" (macroexpand '(m2 5)))
