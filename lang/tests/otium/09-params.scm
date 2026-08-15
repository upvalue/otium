; Dynamic params (spec section 9): defaults, nesting, sequential
; binding, restoration on every exit path.

(defparam depth 0)
(defparam label "top")

; default value
(println "default:" (depth))
(println "default-str:" (label))

; with-params binds dynamically, nests, and restores
(with-params ((depth 1))
  (println "outer-bind:" (depth))
  (with-params ((depth 2) (label "inner"))
    (println "inner-bind:" (depth) (label)))
  (println "after-inner:" (depth) (label)))
(println "after-outer:" (depth) (label))

; binding is sequential: later exprs see earlier bindings in effect
(with-params ((depth 10) (label (str "at-" (depth))))
  (println "sequential:" (label)))

; the binding is visible through function calls (dynamic, not lexical)
(define (report) (depth))
(println "dyn-out:" (report))
(with-params ((depth 5))
  (println "dyn-in:" (report)))
(println "dyn-restored:" (report))

; restored even when the body raises
(try
  (with-params ((depth 99))
    (error "escape"))
  (catch (error? e) nil))
(println "after-raise:" (depth))

; restored when a restart unwinds out of the body
(println "restart-exit:"
  (handler-bind (((type-pred 'error) (lambda (c) (invoke-restart 'out 'ok))))
    (restart-case
        (with-params ((depth 77))
          (error "leave"))
      (out (v) v))))
(println "after-restart:" (depth))

; params are first-class values of type :param
(println "param-type:" (type depth))
