#!/usr/bin/env python3
"""Compare the supported language subset with the primary Chez implementation."""
import pathlib
import subprocess
import tempfile

ROOT = pathlib.Path(__file__).resolve().parent.parent
CASES = [
    '(println (let ((x 10) (y (+ x 2)) (x (* x 2))) (+ x y)))',
    '(println (let ((x 0)) (set! x 42)))',
    '(println (set-car! (cons 1 2) 3))',
    '(println (set-cdr! (cons 1 2) 3))',
    '(println (list (if nil 1 2) (if #f 1 2) (if 0 1 2) (if \'() 1 2)))',
    '(println (list (if #f 1) (and) (or) (and 1 2 3) (or nil #f 7)))',
    '(println (let ((x 0)) (and #f (set! x 1)) (or 2 (set! x 3)) x))',
    '(println (let ((x 0)) (while (< x 100) (set! x (+ x 1))) x))',
    '(println (let ((x 9)) (let loop ((x 1) (y x)) (+ x y))))',
    '(def (counter x) (fn () (set! x (+ x 1)) x)) '
    '(def tick (counter 40)) (println (list (tick) (tick)))',
    '(println (let ((x 17) (outer (fn () (fn () x)))) ((outer))))',
    '(println (let ((x 0)) (def (a) (b)) (def (b) (set! x (+ x 1))) (a) x))',
    '(def (live) 1) (def saved live) (def (caller) (live)) '
    '(def (live) 2) (println (list (saved) (caller)))',
    '(println (list (/ 6 3) (/ 3 2) (modulo -7 3) (remainder -7 3)))',
    '(println (list (> 9007199254740993 9007199254740992.0) '
    '(< 4611686018427387903 4611686018427387904.0) (< -1.5 -1)))',
    '(println (list (eq? \'same \'same) (eq? :same :same) (eq? \'same :same)))',
    '(println (let loop ((i 10000) (total 0)) '
    '(if (= i 0) total (loop (- i 1) (+ total i)))))',
]
CASES += [
    '(println (list [] [1 2] (array? []) (array? nil) (length []) (length [1 2])))',
    '(println (let ((a [1])) (list (push! a 2 3) (put! a 0 7) (pop! []) (get a -1 9) (get a 8 9) (get a :x 9))))',
    '(println (list ([nil] 0 9) ([] 5 9) (push! []) (copy []) (equal? [1] [1.0])))',
    '(println (list (equal? [] []) (equal? [1 [2] \'(3 . 4)] [1 [2] \'(3 . 4)]) (equal? [1] [1 2]) (equal? [1] \'(1))))',
    '(println (let ((a [1])) (push! a 2 3 4) (pop! a) (pop! a) (pop! a) (equal? a [1])))',
    '(println (let ((a [[1]]) (b (copy a))) (put! (get b 0) 0 42) (list a b (eq? a b))))',
    '(println (list (contains? [[1]] [1]) (contains? [nil] nil) (contains? [] nil) (get nil 0 42)))',
    '(println (let ((a []) (alias a) (i 0)) (while (< i 1000) (push! a i) (set! i (+ i 1))) (list (length alias) (alias 999))))',
]

CASES += [
    '(println (let ((t {:a 1 :b 2})) (put! t :a 3) (put! t :b nil) (put! t :b 4) (list t (keys t) (values t) (length t) (contains? t :a) (contains? t 3))))',
    '(println (list (equal? {:a 1} {:a 1}) (get {1 :a} 1.0) (get {nil 42} nil) (get {0.0 42} -0.0)))',
    '(println (let ((a [1]) (b [1]) (t {})) (put! t a 1) (put! t (list a) 2) (list (get t a) (get t b) (get t (list a)) (get t (list b)))))',
    '(println (list (:a {:a 42}) (:a {} 9) (table? {}) (table? [])))',
    '(println (let ((t {:a [1] :b 2}) (c (copy t))) (put! t :b nil) (put! (c :a) 0 40) (list t c)))',
    '(println (let ((t {}) (i 0) (sum 0)) (while (< i 1000) (put! t i (* i 2)) (set! i (+ i 1))) (set! i 0) (while (< i 1000) (set! sum (+ sum (t i))) (put! t i nil) (set! i (+ i 1))) (list sum (length t) (keys t))))',
    '(println (let ((t {:a 1 :b 2 :a 3})) (put! t :a nil) (put! t :a 4) (list (keys t) (values t) (contains? {:f #f} :f))))',
]

CASES += [
    '(println ((fn args args) 1 2 3))',
    '(println ((fn (x . args) (list x args)) 1 2 3))',
    '(println ((fn (x & args) (list x args)) 1))',
    '(def (gather . args) args) (println (list (gather) (gather 1 2 3) (eq? (gather 1) (gather 1))))',
    '(println (let ((f ((fn args (fn () args)) 1 2))) (f)))',
    '(def (loop n previous & args) (if (= n 0) (previous) (loop (- n 1) (fn () args) n))) (println (loop 100 (fn () nil) 99))',
]

CASES += [
    '(println (apply + \'(1 2 3)))',
    '(println (apply list 1 2 [3 4]))',
    '(println (list (apply + nil) (apply list \'())))',
    '(println (apply apply (list + \'(20 22))))',
    '(println (list (apply [42] \'(0)) (apply :x (list {:x 42})) (apply {:x 42} [:x])))',
    '(println (apply (fn (x & rest) (list x rest)) 1 \'(2 3)))',
    '(def (loop n total) (if (= n 0) total (apply loop (- n 1) (list (+ total 1))))) (println (loop 10000 0))',
    '(def saved apply) (def apply saved) (def change (fn () (set! apply (fn (a b) 99)) \'(20 22))) (println (apply + (change))) (set! apply saved)',
]

CASES += [
    '(println ((fn (x &optional (y (+ x 1)) (z (+ y 1))) (list x y z)) 1))',
    '(println ((fn (x &optional (y (+ x 1)) (z (+ y 1))) (list x y z)) 1 20))',
    '(println (let ((x 41)) ((fn (&optional (x (+ x 1))) x))))',
    '(println (list ((fn (&optional (x 42)) x) nil) ((fn (&optional (x 42)) x) #f)))',
    '(println ((fn (&optional (x (cons 41 nil)) (y (fn () (car x)))) (set-car! x 42) (y))))',
    '(def hits 0) (def (f &optional (x (begin (set! hits (+ hits 1)) hits)) (y (begin (set! hits (+ hits 1)) hits))) (list x y)) (println (list (f) (f 20 22) hits (apply f \'(20 22))))',
    '(def (loop n previous &optional (x (cons n nil))) (if (= n 0) (previous) (loop (- n 1) (fn () (car x))))) (println (loop 100 (fn () 99)))',
]

CASES += [
    '(println (list (+ 1 2 3 4 5 6 7 8) (+ 1 2 3 4 5 6 7 8 9) (- 100 20 30 8) (* 2 3 7)))',
    '(println (list (< 1 2 3 4 5 6 7 8) (< 1 2 3 4 4 6 7 8) (<= 1 1 2 3) (>= 8 7 7 1) (> 3 2 1)))',
    '(println (list (= 42 (+ 10 20 12) (* 2 3 7)) (< 1 1.5 2) (= (+ 1 2.5 3) 6.5)))',
    '(def saved +) (def + saved) (def change (fn () (set! + (fn args 99)) 2)) (println (+ 1 (change) 3))',
]

CASES += [
    '(defonce x 1) (defonce x 2) (println x)',
    '(begin (defonce x 1) (begin (defonce x (fn (a a) a)))) (println x)',
    '(def hits 0) (defonce state (begin (set! hits (+ hits 1)) [41])) (defonce state (error "skipped")) (println (list hits state))',
    '(defonce x nil) (defonce x 42) (defonce y #f) (defonce y 42) (println (list x y))',
    '(def x 42) (defonce x 99) (println x)',
    '(defonce x "original" 42) (defonce x "replacement" 99) (println x)',
    '(defonce + (fn (a b) 17)) (defonce + (fn (a b) 99)) (println (+ 1 2))',
    '(defonce state [0]) (def (tick) (put! state 0 (+ (state 0) 1)) (state 0)) (def saved tick) (defonce state [100]) (def (tick) (put! state 0 (+ (state 0) 10)) (state 0)) (println (list (tick) (saved) state))',
]

CASES += [
    '(ns left) (def x 1) (ns right) (def x 2) (println (list left/x x))',
    '(ns left) (def x 1) (def (f) x) (ns right) (set! left/x 42) (println (left/f))',
    '(ns left) (def (f) x) (def x 42) (ns right) (println (left/f))',
    '(ns left) (def (f x) (+ x 1)) (def + (fn (a b) 99)) (println (list (f 2) (+ 1 2)))',
    '(ns left) (def + (fn (a b) 99)) (def (f) (+ 1 2)) (def + (fn (a b) 17)) (ns right) (println (list (left/f) (+ 20 22)))',
    '(ns left) (defonce x nil) (ns right) (defonce x #f) (ns left) (defonce x 42) (println (list x right/x))',
    '(ns left) (def x 41) (ns right) (def left/x 99) (println left/x) (ns left) (println x)',
    '(ns future) (ns left) (def (f) future/x) (ns future) (def x 42) (println (left/f))',
    '(ns left) (println (otium.core/+ 20 22))',
]

INVALID = ['(fn (x &optional) x)', '(fn (&optional x) x)', '(fn (&optional (x 1) & rest) x)', '(fn (&optional (x 1) . rest) x)', '(fn (x &optional (x 1)) x)', '(fn (&optional (x 1) (x 2)) x)', '((fn (&optional (x 1)) x) 1 2)', '(apply +)', '(apply list 1)', "(apply list '(1 . 2))", "(apply 1 '())", "(apply (fn (x) x) '())", '((fn (x & args) args))', '(fn (x & x) x)', '(fn (x . x) x)', '(fn (x &) x)', '(fn (x & y z) x)', '(fn (x & y . z) x)', '(fn (x . 1) x)', '(+ 1 (def invalid 2))', '(let ((x 1 2)) x)', '(fn (x x) x)', '(/ 1 0)', '(put! [] 0 1)', '(push! 1 2)', '([1])', '(get [1])', '(table 1)', '(keys [])', '(put! {} 1)', '(let ((k (cons 1 2))) (get {} k) (set-car! k 9))', '(let ((k (cons 1 2))) (put! {} k nil) (set-cdr! k 9))']

INVALID += ['(defonce)', '(defonce x)', '(defonce 1 2)', '(defonce (f x) x)',
            '(defonce x 2 3)', '(defonce x 1) (defonce x 2 3 4)',
            '(fn () (defonce x 1))', '(let () (defonce x 1))',
            '(+ 1 (defonce x 2))', '(if #t (defonce x 1) 2)']

INVALID += ['(ns)', '(ns 1)', '(ns x y)', '(ns x . y)', '(fn () (ns x))',
            '(let () (ns x))', '(if #t (ns x) nil)', '(def + +)']


def run(command, path):
    return subprocess.run([*map(str, command), str(path)], cwd=ROOT,
                          capture_output=True, text=True, timeout=30)


def main():
    commands = [[ROOT / 'prototype/otium-proto'],
                [ROOT / 'otium', '--runtime=chez', '--no-project']]
    with tempfile.TemporaryDirectory(prefix='otium-differential-') as directory:
        path = pathlib.Path(directory) / 'case.ot'
        for index, source in enumerate(CASES):
            path.write_text(source + '\n')
            results = [run(command, path) for command in commands]
            assert all(r.returncode == 0 for r in results), (source, [r.stderr for r in results])
            assert all(r.stdout == results[0].stdout for r in results), (index, source, [r.stdout for r in results])
        for source in INVALID:
            path.write_text(source + '\n')
            results = [run(command, path) for command in commands]
            assert all(r.returncode != 0 for r in results), source
    print(f'Chez differential checks passed: {len(CASES)} programs, {len(INVALID)} errors')


if __name__ == '__main__':
    main()
