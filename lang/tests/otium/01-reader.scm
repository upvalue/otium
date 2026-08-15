; Reader syntax: quote sugar, collection-literal reading, dotted pairs,
; number forms, string escapes.

; 'x reads as (quote x); ''x is (quote (quote x))
(println "quote-sugar-car:" (car ''x))
(println "quote-sugar-cadr:" (car (cdr ''x)))
(println "quote-sugar-len:" (length ''x))

; '[1 2] is the LIST (array 1 2), not an array
(println "bracket-is-list:" (list? '[1 2]))
(println "bracket-head:" (car '[1 2]))
(println "bracket-second:" (car (cdr '[1 2])))
(println "bracket-len:" (length '[1 2]))
(println "brace-head:" (car '{:a 1}))

; evaluated literals are real collections
(println "array-type:" (type [1 2]))
(println "table-type:" (type {:a 1}))

; dotted pairs and improper lists
(println "pair-car:" (car '(1 . 2)))
(println "pair-cdr:" (cdr '(1 . 2)))
(println "improper-tail:" (cdr (cdr '(a b . c))))
(println "pair-type:" (type '(1 . 2)))
(println "pair-not-list:" (list? '(1 . 2)))

; hex integers
(println "hex-ff:" 0xff)
(println "hex-10:" 0x10)

; floats: integral floats print with .0; shortest round-trip
(println "float-int:" 3.0)
(println "float-exp:" 1e3)
(println "float-neg:" -.5)
(println "float-plain:" 3.5)
(println "float-type:" (type 3.0))
(println "int-type:" (type 3))

; string escapes (measured by length so the expected file stays simple)
(println "esc-len:" (string-length "\n\t\r\0\e\"\\"))
(println "esc-tab:" "a\tb")
(println "esc-quote:" "say \"hi\"")
