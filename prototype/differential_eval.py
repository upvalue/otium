#!/usr/bin/env python3
"""Compare runtime evaluation and macro reentry with Chez."""
import differential

differential.CASES = [
    "(println (+ 2 (eval '(eval '(+ 20 20)))))",
    "(def x 42) (println (let ((x 0)) (eval 'x)))",
    "(eval '(begin (def y 40) (set! y (+ y 2)))) (println y)",
    "(def f (eval '(let ((x 40)) (fn (y) (+ x y))))) (println (f 2))",
    "(println (try (eval '(error :original)) (catch c c)))",
    "(println (eval '(try (error 40) (catch c (+ c 2)))))",
    "(println (try (eval '(if)) (catch c :syntax))) (println (eval '(+ 40 2)))",
    "(eval '(defmacro plus-one (x) `(+ ,x 1))) (println (eval '(plus-one 41)))",
    "(defmacro nested (x) (eval '(+ 20 22))) (println (nested ignored))",
    "(defmacro nested-closure () (eval '(def survivor (fn () 42))) 0) (nested-closure) (println (survivor))",
    "(defmacro nested-caught () (try (eval '(if)) (catch c 42))) (println (nested-caught))",
    "(def a [1 '(+ 1 2)]) (println (list (eq? (eval a) a) (eval a)))",
    "(def t {:x '(+ 1 2)}) (println (list (eq? (eval t) t) (eval t)))",
    "(def f (fn () 42)) (println (eq? (eval f) f))",
    "(def a [1]) (println (eq? (eval (list 'quote a)) a))",
    "(ns first) (def x 1) (def (f) (eval 'x)) (ns second) (def x 42) (println (first/f))",
    "(println (try (eval '#`(let ((x 1)) x)) (catch c (get c :code))))",
]
differential.CASES += [
    "(ns first) (def x 1) (def (g e) (e 'x)) (def e eval) (ns second) (def x 42) (println (list (first/g eval) (first/g first/e) (first/e 'x)))",
    "(defmacro array-result () [1 2]) (println (array-result))",
    "(defmacro closure-result () (fn () 42)) (println ((closure-result)))",
]
differential.INVALID = ["(eval)", "(eval 1 2)", "(eval '(if))", "(eval '(fn (x x) x))", "(eval 'missing-eval-global)"]
if __name__ == '__main__':
    differential.main()
