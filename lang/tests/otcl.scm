(require '(otcl :as otcl))

(define (check expected actual message)
  (unless (equal? expected actual)
    (error message expected actual)))

(define tcl (otcl/make-interpreter))

(check "hello" (otcl/eval-script tcl "set value hello; set value")
       "otcl set/get")
(check "$value" (otcl/eval-script tcl "set literal {$value}; set literal")
       "otcl braced words are literal")
(check "hello-world"
       (otcl/eval-script tcl
         "set suffix world; set joined \"$value-$suffix\"; set joined")
       "otcl quoted interpolation")
(check "14" (otcl/eval-script tcl "expr 2 + 3 * 4")
       "otcl expression precedence")
(check "9" (otcl/eval-script tcl "expr (1 + 2) * 3")
       "otcl expression parentheses")
(check "0.01" (otcl/eval-script tcl "expr 1e-2")
       "otcl expression exponents")
(check "middle"
       (otcl/eval-script tcl
         "if {0} {set branch first} elseif {1} {set branch middle} else {set branch last}; set branch")
       "otcl elseif")

(check "12"
       (otcl/eval-script tcl
         "set x 0; set sum 0
          while {$x < 6} {
            set x [expr $x+1]
            if {$x == 3} {continue}
            if {$x == 6} {break}
            set sum [expr $sum+$x]
          }
          set sum")
       "otcl while, break, and continue")

(check "55"
       (otcl/eval-script tcl
         "proc fib {x} {
            if {$x <= 1} {return $x}
            expr [fib [expr $x-1]] + [fib [expr $x-2]]
          }
          fib 10")
       "otcl procedures and recursion")

(check "7"
       (otcl/eval-script tcl
         "set Total 4
          proc add-global {amount} {set Total [expr $Total+$amount]}
          add-global 3
          set Total")
       "otcl uppercase globals")

(define (join-command interp frame args)
  {:code :ok :value (string-join ":" args)})
(otcl/register-command! tcl "join" join-command)
(check "a:b:c" (otcl/eval-script tcl "join a b c")
       "otcl command registration")
(check "a:b:c"
       (otcl/eval-script tcl "set command jo; set tail in; $command$tail a b c")
       "otcl interpolated command name")
(check "before-7-after"
       (otcl/eval-script tcl "set nested \"before-[expr 3+4]-after\"; set nested")
       "otcl command substitution in quoted words")

(define unknown (otcl/eval-result tcl "does-not-exist"))
(check :error (otcl/result-code unknown) "otcl unknown command status")
(check "No such command 'does-not-exist'" (otcl/result-value unknown)
       "otcl unknown command message")

(check :error (otcl/result-code (otcl/eval-result tcl "set missing $unknown"))
       "otcl missing variable status")
(check :error (otcl/result-code (otcl/eval-result tcl "set x {unterminated"))
       "otcl unterminated brace status")
(check :error (otcl/result-code (otcl/eval-result tcl "set x [set value"))
       "otcl unterminated command substitution status")
