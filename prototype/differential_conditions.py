#!/usr/bin/env python3
"""Condition construction and try behavior against Chez."""
import differential

differential.CASES = [
    '(println (try 42 (catch c 0)))',
    '(println (try (error "boom") (catch (error? e) (list (condition-type e) (condition-message e)))))',
    '(println (list (try (error #f) (catch c c)) (try (error nil) (catch c c)) (try (error 42) (catch c c))))',
    '(println (try (error "data" 1 42) (catch c (get c :data))))',
    '(println (try (error 0) (catch ((fn (c) #f) c) 0) (catch c 42)))',
    '(println (try (try (error 42) (catch ((fn (c) #f) c) 0)) (catch c c)))',
    '(println (try (try (error 1) (catch c (error 42))) (catch c c)))',
    '(println (let ((predicate (fn (c) #t))) (try (error 42) (catch (predicate predicate) predicate))))',
    '(println (try (car 1) (catch (error? e) 42)))',
    '(println (try (apply + \'(1 #t)) (catch (error? e) 42)))',
    '(println (try (error 1) (catch c (def x 40) (def y 2) (+ x y))))',
    '(define-condition \'child \'error) (define-condition :grandchild "child") '
    '(println (list (error? {:type \'grandchild}) (condition-of-type? {:type \'grandchild} \'child) '
    '(condition-of-type? {:type \'child} \'grandchild)))',
    '(define-condition \'child \'error) (println (try (define-condition \'error \'child) (catch (error? e) #t)))',
    '(define-condition \'child \'error) (define-condition \'child nil) (println (error? {:type \'child}))',
    '(println (let ((x [42])) (eq? x (try (error x) (catch c c)))))',
    '(println ((try (error [42]) (catch c (fn () (get c 0))))))',
    '(println (try (error 1 2) (catch (error? e) #t)))',
]
differential.INVALID = ['(define-condition 1 nil)', '(try 1 (catch))', '(try (error 42))']
if __name__ == '__main__':
    differential.main()
