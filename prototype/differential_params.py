#!/usr/bin/env python3
"""Compare dynamic parameter binding and unwinding with Chez."""
import differential

PREFIX = "(defparam p '(default)) (defparam q 0) "
differential.CASES = [PREFIX + source for source in [
    "(println (list (with-params ((p '(first)) (p (cons 'second (p)))) (p)) (p)))",
    "(def (read-p) (p)) (println (with-params ((p '(caller))) (read-p)))",
    "(println (with-params ((p '(outer))) (list (with-params ((p '(inner))) (p)) (p))))",
    "(println (with-params ((p '(inside))) (try (error :caught) (catch c (p)))))",
    "(println (try (with-params ((p '(unwind))) (error :caught)) (catch c (p))))",
    "(println (try (with-params ((p '(first)) (q (/ 1 0))) (p)) (catch c (p))))",
    "(println (try (with-params ((p '(first)) (42 1)) (p)) (catch c (p))))",
    "(println (with-params ((p '(eval))) (eval '(p))))",
    "(println (try (with-params ((p '(eval))) (eval '(error :caught))) (catch c (p))))",
    "(def later (with-params ((p '(temporary))) (fn () (p)))) (println (later))",
    "(println (with-params ((p '(apply))) (apply p '())))",
    "(println (with-params ((p q) ((p) 42)) (q)))",
    "(println (list (with-params ((p 42))) (with-params () 42)))",
    "(def old-p p) (defparam p '(new)) (println (list (eq? old-p p) (old-p) (p)))",
]]
differential.INVALID = ["(with-params)", "(defparam p 1) (with-params ((p)))", "(defparam p 1) (with-params ((p 1 2)) 3)",
    "(defparam)", "(defparam p)", "(defparam 1 2)", "(defparam p 1 2)", "(fn () (defparam p 1))", "(let () (defparam p 1))"]
if __name__ == '__main__':
    differential.main()
