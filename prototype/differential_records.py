#!/usr/bin/env python3
"""Compare fixed-layout records and declaration reloads with Chez."""
import differential

differential.CASES = [
    '(defrecord P (x y)) (def p (record-new P 1 2)) (println (list (type P) (type p) (record? p) (record-type? P) (eq? (record-type p) P))) (println (set! (p :x) 3)) (println (apply p (list :x)))',
    '(defrecord P (x)) (def p (record-new P 1)) (def t {p :found}) (record-set! p :x 2) (println (list (get t p) (equal? p (record-new P 2)) (table? p)))',
    '(defrecord P (x) :immutable #t) (def p (record-new P [])) (push! (p :x) 1) (println (p :x)) (println (try (record-set! p :x []) (catch e :immutable)))',
    '(defrecord P ()) (def p (record-new P)) (def saved P) (defrecord P ()) (println (list (eq? saved P) (eq? (record-type p) P)))',
    '(defrecord P (x y)) (def old P) (def p (record-new P 1 2)) (println (try (eval \'(defrecord P (y x))) (catch e :changed))) (println (list (eq? P old) (p :x) (p :y)))',
    '(def names (list \'x)) (def P (%make-record-type \'Raw names #f)) (set-car! names \'y) (println ((record-new P 42) :x))',
]
differential.INVALID = [
    '(defrecord P (x x))', '(defrecord P (1))',
    '(defrecord P (x) :immutable :yes)', '(defrecord P (x)) (record-new P)',
    '(defrecord P (x)) (record-new P 1 2)', '(record-type {})',
    '(defrecord P (x)) ((record-new P 1) :missing)',
    '(defrecord P (x) :immutable #t) (set! ((record-new P 1) :x) 2)',
]
if __name__ == '__main__':
    differential.main()
