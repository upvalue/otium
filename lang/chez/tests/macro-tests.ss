(import (chezscheme) (prefix (chezscheme) chez/) (otium runtime))

(define checks 0)
(define (check label expected actual)
  (unless (equal? expected actual) (error 'check label expected actual))
  (set! checks (+ checks 1)))
(define (fails? thunk)
  (guard (c [else #t]) (thunk) #f))
(define (fails-with? message thunk)
  (guard (c [else (and (message-condition? c)
                      (equal? message (condition-message c)))])
    (thunk) #f))
(define (run form) (evaluate '(require chez)) (evaluate form))
(define directory (cadr (command-line)))
(load-path (list directory))
(define (source name forms)
  (let ([path (string-append directory "/" name ".ot")])
    (when (file-exists? path) (delete-file path))
    (call-with-output-file path
      (lambda (port)
        (for-each (lambda (form) (write form port) (newline port)) forms)))))

(run '(defmacro twice (expression)
        (let ([temporary (gensym "temporary")])
          `(let ([,temporary ,expression]) (+ ,temporary ,temporary)))))
(check "macro value" #t (run '(macro? twice)))
(check "procedure is not a macro" #f (run '(macro? +)))
(check "macro runs" 42 (run '(twice 21)))
(check "gensym avoids capture" 42 (run '(let ([temporary 21]) (twice temporary))))
(check "argument evaluated once" 2
  (run '(let ([count 0]) (twice (begin (set! count (+ count 1)) count)))))
(check "macro value cannot be applied" #t (fails? (lambda () (run '(apply twice '(21))))))
(check "arity error has macro context" #t
  (fails-with? "macro `twice` failed while expanding" (lambda () (run '(twice)))))
(run '(defmacro capture (expression) `(+ base ,expression)))
(check "intentional use-site capture" 42 (run '(let ([base 40]) (capture 2))))
(run '(defmacro sum expressions `(+ ,@expressions)))
(run '(defmacro first-plus-rest (first . rest) `(+ ,first ,@rest)))
(check "rest parameters" 6 (run '(sum 1 2 3)))
(check "dotted parameters" 6 (run '(first-plus-rest 1 2 3)))
(run '(defmacro one-step (x) `(twice ,x)))
(check "head-only expansion" '(twice 3) (run '(macroexpand-1 '(one-step 3))))
(check "no descent in macroexpand" '(+ (twice 3) 1)
  (run '(macroexpand '(+ (twice 3) 1))))
(check "quote is data" '(twice 21) (run ''(twice 21)))
(check "quasiquote active unquotes" '(a 42 4 6)
  (run '`(a ,(twice 21) ,@(list (twice 2) (twice 3)))))
(check "quasiquote dotted tail" '(a . 42) (run '`(a . ,(twice 21))))
(check "nested quasiquote stays data" '(quasiquote ((unquote (twice 3))))
  (run '`(quasiquote ((unquote (twice 3))))))
(check "nested active unquote" '(quasiquote ((unquote 6)))
  (run '`(quasiquote ((unquote (unquote (twice 3)))))))

(check "lambda shadows macro" 8 (run '((fn (twice) (twice 8)) (fn (x) x))))
(for-each
  (lambda (form)
    (check "core form names cannot be rebound" #t
      (fails-with? "macroexpand: cannot rebind a core form name" (lambda () (run form)))))
  '((let ([if +]) (if 1 2))
    ((fn (quote) quote) 1)
    (let () (def (fn x) x) (fn 21))
    (defmacro begin () 1)))
(check "rest parameter shadows macro" 8
  (run '((fn twice ((car twice) 8)) (fn (x) x))))
(check "sequential let initializer shadows macro" 3
  (run '(let ([twice (fn (x) x)] [result (twice 3)]) result)))
(check "let body shadows macro" 3
  (run '(let ([twice (fn (x) x)]) (twice 3))))
(check "let binds sequentially" 3
  (run '(let ([a 1] [b (+ a 1)] [a (+ b 1)]) a)))
(check "named let initializers use outer macro" 6
  (run '(let twice ([n (twice 3)]) (if (= n 0) 6 (twice (- n 1))))))
(check "all internal names bind before expansion" 3
  (run '(let () (def (f) (twice 3)) (def (twice x) x) (f))))
(check "internal begin definitions" 3
  (run '(let () (begin (def (f) (twice 3)) (def (twice x) x)) (f))))
(check "definitions may follow expressions" '(2 1)
  (run '(let () (def a 1) (def seen (list a)) (set! a 2) (def b a) (cons b seen))))
(check "body ending in a definition yields nil" #t
  (run '(nil? ((fn () (def x 1))))))
(check "operator expression expands" 6
  (run '((if #t (fn (x) (twice x)) +) 3)))
(check "set value expands" 6
  (run '(let ([x 0]) (set! x (twice 3)) x)))

(run '(def expansions 0))
(run '(defmacro counted (x) (set! expansions (+ expansions 1)) `(+ ,x 1)))
(run '(def (expanded-once x) (counted x)))
(run '(expanded-once 1))
(run '(expanded-once 2))
(check "function body expands once" 1 (run 'expansions))
(run '(defmacro counted (x) `(+ ,x 10)))
(check "old compiled expansion" 2 (run '(expanded-once 1)))
(check "new expansion" 11 (run '(counted 1)))
(run '(defmacro install (name value) `(def ,name ,value)))
(run '(install installed 42))
(run '(defonce installed 99))
(check "generated definition registered" 42 (run 'installed))
(run '(defmacro install-once (name value) `(defonce ,name ,value)))
(run '(defmacro fail-expansion () (error "must not expand")))
(run '(install-once installed (fail-expansion)))
(check "defonce skips expansion" 42 (run 'installed))
(check "generated definition import collision" #t
  (begin
    (source "generated" '((ns generated)
                          (defmacro install (name value) `(def ,name ,value))
                          (install answer 42)))
    (run '(require generated :as g))
    (fails-with? "definition of `g/answer` would replace an import"
      (lambda () (run '(install g/answer 99))))))
(check "generated definition is exported" 42 (run 'g/answer))
(check "generated body definitions" 1 (run '(let () (install x 1) x)))
(check "definitions reject expression context" #t
  (fails-with? "macroexpand: definition is only allowed at top level or in a body"
    (lambda () (run '(if #t (install x 1) 2)))))
(check "generated directives reject expression context" #t
  (fails-with? "macroexpand: form requires top-level context"
    (lambda () (run '(fn () (install-once x 1))))))
(run '(defmacro install-sequence ()
        '(begin (defmacro later (x) `(+ ,x 1)) (def sequence-result (later 41)))))
(run '(install-sequence))
(check "generated begin processes in order" 42 (run 'sequence-result))
(run '(defmacro with-values () '(begin (chez/values 1) (chez/values 2 3))))
(check "generated begin preserves values" '(2 3)
  (call-with-values (lambda () (run '(with-values))) list))
(run '(defmacro change-space () '(begin (ns switched) (def marker 42))))
(run '(change-space))
(check "generated ns changes following environment" 42 (namespace-ref 'switched 'marker))
(run '(ns user))
(check "Otium eval expands" 42 (run '(eval '(twice 21))))
(run '(eval '(install from-eval 7)))
(run '(defonce from-eval 99))
(check "Otium eval registers definitions" 7 (run 'from-eval))

(define (macro-source increment)
  (source "macro-lib"
    `((ns macro-lib)
      (def (helper x) (list '+ 'base x))
      (defmacro add-base (x) (helper x))
      (defmacro increment (x) (list '+ x ,increment))
      (def (identity x) x)
      (defmacro explode () (error "failure")))))
(macro-source 1)
(run '(require macro-lib :as m :refer (increment)))
(run '(def (helper x) (error "wrong namespace")))
(check "imported transformer helper and use-site binding" 42
  (run '(let ([base 40]) (m/add-base 2))))
(check "qualified macro value" #t (run '(macro? m/increment)))
(check "referred macro value" #t (run '(macro? increment)))
(check "imported procedure operands expand" 42 (run '(m/identity (twice 21))))
(run '(def (old-caller x) (m/increment x)))
(macro-source 10)
(run '(require macro-lib :as m :refer (increment) :reload))
(check "old imported expansion retained" 2 (run '(old-caller 1)))
(check "new qualified expansion sees reload" 11 (run '(m/increment 1)))
(check "new referred expansion sees reload" 11 (run '(increment 1)))
(check "qualified names cannot be bound locally" #t
  (fails-with? "macroexpand: cannot bind a qualified name"
    (lambda () (run '(let ([m/increment (fn (x) x)]) (m/increment 4))))))
(check "transformer failure" #t
  (fails-with? "macro `explode` failed while expanding" (lambda () (run '(m/explode)))))
(check "transformer failure restores namespace" 'user (namespace-name (current-namespace)))
(source "macro-consumer"
  '((ns macro-consumer)
    (defmacro setup () '(begin (require macro-lib :as m) (def result (m/increment 2))))
    (setup)))
(run '(require macro-consumer :as consumer))
(check "generated require precedes expansion" 12 (run 'consumer/result))
(source "macro-broken" '((ns macro-broken) (defmacro fail () (error "broken")) (fail)))
(check "failed macro load" #t (fails? (lambda () (run '(require macro-broken)))))
(check "failed load restores caller" 'user (namespace-name (current-namespace)))
(run '(defmacro recurse () '(recurse)))
(check "unbounded expansion diagnosed" #t
  (fails-with? "macro expansion limit exceeded in `recurse`" (lambda () (run '(recurse)))))
(check "fresh expansion budget after failure" 42 (run '(twice 21)))
(check "malformed binding list rejected" #t
  (fails? (lambda () (run '(let ([x 1 2]) x)))))
(check "malformed macro parameters rejected" #t
  (fails? (lambda () (run '(defmacro bad (x x) x)))))
(check "reserved macro name rejected" #t
  (fails-with? "macroexpand: cannot rebind a core form name"
    (lambda () (run '(defmacro if args 1)))))
(run '(defmacro several-values () (chez/values 1 2)))
(check "transformer must return one datum" #t
  (fails-with? "macro `several-values` failed while expanding" (lambda () (run '(several-values)))))
(run '(defmacro malformed () '(if)))
(check "invalid expansion cannot install definition" #t
  (fails? (lambda () (run '(def invalid (malformed))))))
(run '(defonce invalid 42))
(check "failed definition can be retried" 42 (run 'invalid))
(run '(defmacro outer (x) `(inner ,x)))
(run '(defmacro inner (x) `(+ ,x 1)))
(check "macroexpand repeats at head" '(+ 2 1) (run '(macroexpand '(outer 2))))
(run '(def saved-macro inner))
(run '(defmacro inner (x) `(+ ,x 10)))
(check "saved macro retains transformer" 3 (run '(saved-macro 2)))
(check "redefined macro changes future expansion" 12 (run '(inner 2)))
(run '(def inner (fn (x) x)))
(check "macro can become procedure" 2 (run '(inner 2)))
(run '(defmacro inner (x) `(+ ,x 100)))
(check "procedure can become macro" 102 (run '(inner 2)))
(run '(defmacro build-code (x) `(list 'quote ,x)))
(run '(defmacro compile-transformer (x) (build-code x)))
(run '(defmacro build-code (x) '(quote changed)))
(check "transformer body expands when defined" '(quote x)
  (run '(macroexpand-1 '(compile-transformer x))))
(check "referred macro cannot be overwritten" #t
  (fails-with? "definition of `increment` would replace an import"
    (lambda () (run '(defmacro increment (x) x)))))
(run '(ns macro-lib))
(run '(def (helper x) (list '+ 'base (* 2 x))))
(run '(ns user))
(check "macro sees updated defining helper" 44
  (run '(let ([base 40]) (m/add-base 2))))
(source "wrong-macro-ns"
  '((defmacro wrong () '(ns incorrect)) (wrong)))
(check "generated ns respects required file name" #t
  (fails-with? "file declares namespace `incorrect` but was loaded as `wrong-macro-ns`"
    (lambda () (run '(require wrong-macro-ns)))))
(check "failed generated ns restores caller" 'user (namespace-name (current-namespace)))
(source "own-eval"
  '((ns own-eval)
    (def base 40)
    (defmacro add-own (x) (eval `(list '+ base ',x)))))
(run '(require own-eval :as own))
(check "eval in transformer uses defining namespace" 42 (run '(own/add-own 2)))
(check "Otium eval does not capture lexical bindings" #t
  (fails? (lambda () (run '(let ([only-local 1]) (eval 'only-local))))))
(run '(def (count-down n) (if (= n 0) 42 (count-down (- n 1)))))
(run '(defmacro tail-call (n) `(count-down ,n)))
(check "expanded tail call" 42 (run '(tail-call 100000)))
(printf "~a macro checks passed\n" checks)
