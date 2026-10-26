#!/usr/bin/env python3
"""Compare property descriptions and state lifecycle with Chez."""
import differential

differential.CASES = [
    '(defclass W (p :init 1) (s :state #t :init 2)) (def d (class-properties W)) (defclass W (p :init 3) (s :state #t :init 4) (z :init 5)) (println (list (d :p) (d :s) (d :z)))',
    '(def n 0) (defclass W (p :init []) (s :init (begin (set! n (+ n 1)) []) :state #t)) (def d (class-properties W)) (println (list n (d :s))) (def a (class-mount d)) (def b (class-mount d)) (println (list n (eq? (a :p) (b :p)) (eq? (a :s) (b :s))))',
    '(defclass W p (s :init 1 :state #t)) (def a (class-mount (class-properties W :p :old))) (slot-set! a :s 9) (def result (class-update-properties! a (class-properties W :p :new))) (println (list (eq? result a) (a :p) (a :s)))',
    '(defclass W p (s :init 1 :state #t)) (def a (class-mount (class-properties W :p :old))) (defclass W q (s :init 2 :state #t) (z :init 3 :state #t)) (defmethod (update-instance (a W) old) (println (list (a :q) (get old :p)))) (class-update-properties! a (class-properties W :q :new)) (println (list (a :q) (a :s) (a :z)))',
    '(defclass W (p :init 1) (s :init 2 :state #t)) (def a (make W :p 9 :s 8)) (defclass W (p :init 3 :state #t) (s :init 4)) (class-update-properties! a (class-properties W :s 7)) (println (list (a :p) (a :s)))',
    '(defclass W p (s :init 1 :state #t)) (def a (make W :p :old)) (defclass W p (s :init 2 :state #t) (z :init 3 :state #t)) (defmethod (update-instance (a W) old) (slot-set! a :s 99) (error :retry)) (println (try (class-update-properties! a (class-properties W :p :new)) (catch e :failed))) (defmethod (update-instance (a W) old) (println (get old :p))) (class-update-properties! a (class-properties W :p :retry)) (println (list (a :p) (a :s)))',
]
differential.INVALID = [
    '(defclass W (s :init 1 :state #t)) (class-properties W :s 2)',
    '(defclass W p) (class-properties W)',
    '(class-mount {})',
    '(defclass W p) (def d (class-properties W :p 1)) (defclass W p q) (class-mount d)',
    '(defclass W) (defclass V) (class-update-properties! (make W) (class-properties V))',
]
if __name__ == '__main__':
    differential.main()
