#!/usr/bin/env python3
"""Compare memory port byte/character mixing and dynamic output with Chez."""
import differential

differential.CASES = [
    '(def p (open-input-string "é🙂\\nlast")) (println (list (peek-char p) (peek-u8 p) (read-u8 p) (read-u8 p) (read-line p) (read-all p) (read-all p) (eof-object? (read-char p))))',
    '(println (with-output-to-string (fn () (println "hello" 3) (write "x") (display #\\a))))',
    '(println (with-output-to-string (fn () (display (with-output-to-string (fn () (display "nested")))))))',
    '(def b #u8(65)) (def p (open-input-bytevector b)) (put! b 0 66) (println (read-u8 p))',
    '(def p (open-output-bytevector)) (write-u8 255 p) (write-bytevector #u8(0 1 2 3) p 1 3) (println (get-output-bytevector p) (get-output-bytevector p))',
    '(def p (open-input-bytevector #u8(0 1 2 255))) (def b (make-bytevector 6 9)) (def n (read-bytevector! b p 1 6)) (def eof (eof-object? (read-bytevector! b p))) (println n b eof)',
    '(def p (open-input-string "  123,é🙂!")) (def a (skip-while " " p)) (def b (read-while "0123456789" p)) (def c (read-until "🙂" p)) (def d (read-all p)) (println a b c d)',
    '(def p (open-input-string "a\\r\\nb\\n\\nlast\\r")) (println (list (read-line p) (read-line p) (read-line p) (read-line p) (eof-object? (read-line p))))',
    "(def p (open-input-bytevector #u8(65 255))) (read-char p) (try (read-char p) (catch e (println (condition-type e) (get e :position) (read-u8 p))))",
    "(def p (open-input-string \"x\")) (close-port p) (close-port p) (try (read-char p) (catch e (println (condition-type e) (get e :reason))))",
]
differential.CASES += [
    "(def p (open-input-string \"(a)tail\")) (def a (read p)) (def b (read p)) (println a b)",
    "(def p (open-input-string \"#u8(1 (+ 1 2)) :tail\")) (def a (read p)) (def b (read p)) (println a b (eof-object? (read p)))",
    "(def p (open-input-string \"' ; comment\\n (a . b)\")) (println (read p))",
    "(println (eof-object? (read (open-input-string \"; only\"))))",
]
differential.CASES += [
    "(def b #u8(65 195 169 240 159 153 130)) (def i 0) (def p (make-input-port \"bytes\" {:read (fn (out s e) (if (= i (length b)) (eof-object) (begin (put! out s (get b i)) (set! i (+ i 1)) 1)))})) (println (read-all p)) (close-port p)",
    "(def sent []) (def attempts 0) (def p (make-output-port \"retry\" {:write (fn (b s e) (set! attempts (+ attempts 1)) (when (= attempts 2) (error :retry)) (push! sent (get b s)) 1)})) (write-bytevector #u8(7 8 9) p) (try (flush-output-port p) (catch e nil)) (flush-output-port p) (close-port p) (println sent)",
    "(def n 0) (def p (make-output-port \"large\" {:write (fn (b s e) (set! n (+ n (- e s))) (- e s))})) (write-string (string-repeat \"é\" 5000) p) (flush-output-port p) (close-port p) (println n)",
    "(def p (make-input-port \"closed\" {:read (fn (b s e) (eof-object))})) (println (contains? (open-ports) p)) (close-port p) (println (contains? (open-ports) p))",
    "(println (contains? (open-ports) (open-output-string)))",
]
differential.INVALID = ['(read-string -1)', '(read-bytevector -1)', '(write-u8 256 (open-output-bytevector))', '(get-output-string (open-output-bytevector))', '(write-bytevector #u8(1) (open-output-bytevector) 1 0)', '(read-u8 (open-output-string))', '(close-output-port (open-input-string ""))']
differential.INVALID += ['(read (open-input-string "(a]"))', '(read (open-input-string "\\\"unfinished"))', '(read (open-output-string))', '(read 1 2)']
if __name__ == '__main__':
    differential.main()
