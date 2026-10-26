#!/usr/bin/env python3
"""Compare class construction and lazy migration with Chez."""
import differential

differential.CASES = [
    '(defabstract A) (defclass B :is A x (y :init [] :accessor y-of)) (def b (make B :x 7)) (println (list (is-a? b A) (b :x) (y-of b) (type b) (table? b)))',
    '(defclass B (x :init [])) (def a (make B)) (def b (copy a)) (println (list (eq? a b) (eq? (a :x) (b :x))))',
    '(defclass B x) (def b (make B :x 1)) (println (set! (b :x) 7)) (println (apply b (list :x)))',
    '(defclass B (x :init 1)) (def b (make B)) (def saved B) (defclass B (z :init []) (x :init 2)) (println (list (eq? B saved) (b :x) (b :z)))',
    '(defclass B (x :init 1)) (def b (make B)) (defclass B y) (defmethod (update-instance (b B) old) (slot-set! b :y (get old :x))) (println (b :y))',
    '(defclass B (x :init 1)) (def b (make B)) (defclass B x (y :init 2)) (defmethod (update-instance (b B) old) (slot-set! b :x 99) (error :retry)) (println (try (b :y) (catch e :failed))) (defmethod (update-instance (b B) old) nil) (println (list (b :x) (b :y)))',
    '(defabstract A) (defabstract C) (defclass B :is A) (def b (make B)) (defabstract A :is C) (println (list (is-a? b A) (is-a? b C)))',
    '(defclass B (x :init 1)) (def b (make B :x 9)) (defclass B (x :init 2 :state #t)) (println (b :x))',
]
differential.INVALID = [
    '(defclass B x x)', '(defabstract A x)',
    '(defclass B) (defclass C :is B)',
    '(defabstract A) (defabstract B :is A) (defabstract A :is B)',
    '(defclass B x) (make B)', '(defclass B x) (make B :z 1)',
    '(defclass B (x :state #t))', '(defclass B (x :init 1 :init 2))',
]
if __name__ == '__main__':
    differential.main()
