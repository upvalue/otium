#!/usr/bin/env python3
"""Compare bytevector mutation, slicing and UTF-8 behavior with Chez."""
import differential

differential.CASES = [
    '(println (bytevector 0 127 255) (make-bytevector 3 7) (bytevector-append))',
    '(def b #u8(0 1 2 3 4)) (bytevector-copy! b 1 b 0 4) (println b) (bytevector-copy! b 0 b 1 5) (println b)',
    '(def b #u8(1 2)) (println (list (eq? b (bytevector-copy b)) (equal? b #u8(1 2)) (equal? b b)))',
    '(def b #u8(1 2)) (def t (table b :found)) (put! b 0 3 1 4) (println (list b (get t b) (get t #u8(3 4))))',
    '(println (string->utf8 "aé🙂\\0" 1 3) (utf8->string #u8(0 195 169 0) 1 3))',
    '(println (list (get #u8(1) -1) (get #u8(1) 1 :missing) (get #u8(1) 0.0) (bytevector-length #u8())))',
    '(println (bytevector-append #u8(1) #u8() #u8(2 3)) (bytevector-copy #u8(1 2 3) 1 1))',
    "(try (utf8->string #u8(0 65 255) 1) (catch e (println (list (condition-type e) (get e :position) (condition-of-type? e 'io-error)))))",
    "(for-each (fn (b) (try (utf8->string b) (catch e (println (condition-type e))))) (list #u8(128) #u8(192 128) #u8(194) #u8(224 128 128) #u8(237 160 128) #u8(240 128 128 128) #u8(244 144 128 128) #u8(226 40 161)))",
    '(println (utf8->string (string->utf8 (list->string (map integer->char \'(0 127 128 2047 2048 55295 57344 65535 65536 1114111))))))',
]
differential.INVALID = ['(bytevector 256)', '(bytevector 1.0)', '(make-bytevector -1)', '(bytevector-copy #u8(1) 1 0)', '(bytevector-copy! #u8(1) 1 #u8(2))', '(utf8->string #u8(1) 0 2)', '(string->utf8 "é" 2)', '(bytevector-u8-ref #u8(1) 1)', '(bytevector-u8-set! #u8(1) 0 -1)']
if __name__ == '__main__':
    differential.main()
