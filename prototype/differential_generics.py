#!/usr/bin/env python3
"""Compare primary-method selection and replacement with Chez."""
import differential

differential.CASES = [
    '(println (list (eq? (dispatch-type 42) Number) (eq? (dispatch-type []) Vector) (is-a? Number Object)))',
    '(defgeneric (f x)) (defmethod (f x) 1) (defmethod (f (x Number)) (+ x 1)) (println (list (f 41) (f "x") (apply f \'(41))))',
    '(defgeneric (f x)) (def saved f) (defmethod (f (x Number)) (+ x 1)) (defgeneric (f x)) (defmethod (f (x Number)) (+ x 2)) (println (list (eq? saved f) (saved 40)))',
    '(defgeneric (f x y)) (defmethod (f (x Number) y) 1) (defmethod (f x (y Number)) 2) (println (try (f 1 2) (catch c :ambiguous))) (defmethod (f (x Number) (y Number)) 42) (println (f 1 2))',
    '(defgeneric (f x)) (println (try (f 1) (catch c :missing)))',
    '(defgeneric (f a b c d e)) (defmethod (f a b c d (e Number)) (+ a b c d e)) (println (f 1 2 3 4 5))',
    '(defgeneric (f)) (defmethod (f) 42) (println (f))',
    '(defgeneric (f x)) (defmethod (f (x Number)) x) (println (length (generic-methods f)))',
    '(defmacro source (x) `(+ ,x 1)) (def alias source) (println (alias 41))',
    '(println (list (reverse [1 2 3]) (reverse \'(1 2 3)) (append \'(1 2) \'(3 4))))',
]
differential.INVALID = ['(defgeneric (f x x))', '(defgeneric (f x)) (defmethod (f (x 1)) x)', '(defgeneric (f x)) (f)', '(defgeneric (f x)) (defmethod (f x y) x)']
if __name__ == '__main__':
    differential.main()
