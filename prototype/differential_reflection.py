#!/usr/bin/env python3
"""Compare value types, binding documentation, EOF and namespace reflection with Chez."""
import differential

differential.CASES = [
    "(println (map type (list nil '() #f 1 1.0 'a :a #\\a \"a\" '(a) [] {} (buffer) +)))",
    "(defmacro m () 1) (println (macro? m))",
    "(println (list (type (eof-object)) (eof-object? (eof-object)) (eof-object? nil) (eq? (eof-object) (eof-object)) (if (eof-object) #t #f)))",
    "(println (str [(eof-object)]))",
    "(def answer \"The answer.\" 42) (println (describe 'answer)) (def answer 43) (println (describe 'answer))",
    "(def (square x) \"Square.\" (* x x)) (println (describe 'square))",
    "(defmacro twice (x) \"Twice.\" `(+ ,x ,x)) (println (describe 'twice))",
    "(defonce state \"First.\" []) (defonce state \"Second.\" 1) (println (describe 'state))",
    "(println (describe 'missing-reflection-name))",
    "(ns first) (def x \"one\" 1) (def (where) (current-ns)) (def (help) (describe 'x)) (ns second) (def x \"two\" 2) (println (list (first/where) (first/help)))",
    "(println (put! [1 2] 0 3 1 4))",
    "(def t {}) (put! t 1 :int 1.0 :float :a 1 :a 2 :b nil) (println (list (length t) (get t 1) (get t 1.0) (get t :a)))",
]
differential.INVALID = ["(describe 1)", "(type)", "(eof-object 1)", "(buffer?)", "(put! {} :a)", "(put! [1] 0)"]
if __name__ == '__main__':
    differential.main()
