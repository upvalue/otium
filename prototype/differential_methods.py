#!/usr/bin/env python3
"""Compare lexical next-method snapshots and qualifiers with Chez."""
import differential

differential.CASES = [
    '(defgeneric (f x)) (defmethod (f x) (list :base x)) (defmethod (f (x Number)) :next n (cons :number (n (+ x 1)))) (defmethod :around (f (x Number)) :next n (cons :around (n x))) (println (f 1))',
    '(defgeneric (f x)) (defmethod (f x) :old) (defmethod (f (x String)) :excluded) (defmethod (f (x Number)) :next n n) (def saved (f 1)) (remove-method! f (list Object)) (defmethod (f x) :new) (println (list (saved 1) (saved "x") ((f 1) 1)))',
    '(defgeneric (f x)) (defmethod (f x) :old) (defmethod (f (x Number)) :next n n) (def saved (f 1)) (def old f) (reset-generic! (f x)) (defmethod (f x) :reset) (println (list (saved 1) (f 1) ((old 1) 1) (eq? old f)))',
    '(defgeneric (f x y)) (defmethod (f (x Number) y) 1) (defmethod (f x (y Number)) 2) (defmethod (f (x Number) (y Number)) :next n n) (def next (f 1 2)) (println (procedure? next)) (println (try (next 1 2) (catch e :ambiguous))) (println (next 1 "x"))',
    '(defgeneric (f)) (defmethod (f) 42) (defmethod :around (f) :next n (n)) (println (f)) (println (remove-method! f (list) :around)) (println (f))',
    '(defgeneric (f x)) (defmethod :around (f x) :next n (if n (n x) :alone)) (println (f 1))',
    '(defgeneric (f x)) (defmethod (f x) x) (defmethod (f (x Number)) :next n (let ((r (n x))) (set! n #f) r)) (println (list (f 1) (f 2)))',
    '(defgeneric (f x)) (defmethod (f x) x) (defmethod (f (x Number)) :next n (apply n (list (+ x 1)))) (println (f 41))',
]
differential.INVALID = [
    '(defgeneric (f x)) (defmethod (f x) :next other/n x)',
    '(defgeneric (f x)) (defmethod (f x) :next x x)',
    '(defgeneric (f x)) (defmethod (f x) :next n)',
    '(defgeneric (f x)) (defmethod (f x) :next 42 x)',
    '(defgeneric (f x)) (remove-method! f (list Number) :bogus)',
    '(defgeneric (f x)) (remove-method! f (list Number Number))',
    '(defgeneric (f x)) (defmethod (f x) x) (defmethod (f (x Number)) :next n (n)) (f 1)',
]
if __name__ == '__main__':
    differential.main()
