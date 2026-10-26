#!/usr/bin/env python3
"""Compare macro expansion behavior with the primary implementation."""
import differential

differential.CASES = [
    '(defmacro twice (x) `(+ ,x ,x)) (println (twice 21))',
    '(defmacro twice (x) `(+ ,x ,x)) (def (f x) (twice x)) (println (f 21))',
    '(defmacro twice (x) `(+ ,x ,x)) (println (let ((twice (fn (x) x)) (y (twice 7))) y))',
    '(defmacro twice (x) `(+ ,x ,x)) (println (let ((twice (fn (x) x))) ((fn () (twice 7)))))',
    '(defmacro outer (x) `(inner ,x)) (defmacro inner (x) `(+ ,x 1)) (println (outer 41))',
    '(defmacro install (name value) `(def ,name ,value)) (install answer 42) (println answer)',
    '(defmacro add-all args `(+ ,@args)) (println (add-all 10 20 12))',
    '(defmacro empty (x)) (println (empty 42))',
    '(defmacro nothing () nil) (println (nothing))',
    '(defmacro keyword () :k) (println (keyword))',
    '(ns other) (defmacro add (x) `(+ ,x 1)) (ns user) (println (other/add 41))',
    '(defmacro twice (x) `(+ ,x ,x)) (def (f x) (twice x)) '
    '(defmacro twice (x) `(+ ,x 1)) (println (list (f 21) (twice 41)))',
]
differential.CASES += [
    "(defmacro install (name value) `(def ,name ,value)) (println (let () (install x 7) x))",
    "(println ((fn () (begin (def (f) (g)) (def (g) 3)) (f))))",
    "(println ((fn () (def x 1))))",
    "(defmacro twice (x) #`(let ((t# ,x)) (+ t# t#))) (println (twice 21))",
    "(println (let ((g (gensym))) (eq? g (string->symbol (symbol->string g)))))",
    "(defmacro a (x) `(b ,x)) (defmacro b (x) `(+ ,x 1)) (println (macroexpand-1 '(a 2))) (println (macroexpand '(a 2)))",
]
differential.INVALID = ['(defmacro)', '(defmacro bad)',
                        '(defmacro one (x) x) (one)']
if __name__ == '__main__':
    differential.main()
