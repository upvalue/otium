; Conditions and restarts (spec section 8).

; try/catch the basic error convention
(println "try-catch:"
  (try (error "boom")
    (catch ((type-pred 'error) e) (condition-message e))))

; try returns the body value when nothing raises
(println "try-normal:" (try 7 (catch (error? e) "caught")))

; condition helpers
(println "cond-type:"
  (try (error "msg")
    (catch (error? e) (condition-type e))))
(println "error-data:"
  (try (error "msg" 1 2)
    (catch (error? e) (get e :data))))

; handler-bind: handler runs AT THE SIGNAL SITE and may decline by
; returning; the condition then keeps going and try catches it.
(handler-bind (((type-pred 'error)
                (lambda (c) (println "handler-saw:" (condition-message c)))))
  (try (error "oops")
    (catch (error? e) (println "then-caught:" (condition-message e)))))

; signal with no takers returns nil
(println "signal-unhandled:" (signal "quiet"))

; restart-case + invoke-restart from a handler resumes with the clause
; value: the whole restart-case yields 42 even though (error) was inside.
(println "restart-resume:"
  (handler-bind (((type-pred 'error)
                  (lambda (c) (invoke-restart 'use-value 42))))
    (restart-case (+ 1000 (error "unusable"))
      (use-value (v) v))))

; restart-case returning normally: restarts vanish, value passes through
(println "restart-normal:"
  (restart-case (+ 1 2)
    (use-value (v) v)))

; innermost restart of a name wins
(println "restart-innermost:"
  (handler-bind (((type-pred 'error)
                  (lambda (c) (invoke-restart 'pick "inner"))))
    (restart-case
        (restart-case (error "x")
          (pick (v) (str "inner-got-" v)))
      (pick (v) (str "outer-got-" v)))))

; restart introspection
(restart-case
    (let ((r (find-restart 'my-restart)))
      (println "find-restart-name:" (restart-name r))
      (println "find-restart-desc:" (restart-description r))
      (println "find-restart-type:" (type r))
      (println "find-missing:" (find-restart 'no-such-restart)))
  (my-restart "helps humans" (v) v))

; unwind-protect: cleanups run inner-first while unwinding to a catch
(try
  (unwind-protect
    (unwind-protect (error "unwinding") (println "cleanup-inner"))
    (println "cleanup-outer"))
  (catch (error? e) (println "after-cleanups:" (condition-message e))))

; unwind-protect returns the protected value on the normal path
(println "unwind-value:" (unwind-protect 7 (println "cleanup-normal")))

; cleanups also run when a restart unwinds past them
(println "restart-thru-cleanup:"
  (handler-bind (((type-pred 'error)
                  (lambda (c) (invoke-restart 'give 5))))
    (restart-case
        (unwind-protect (error "x") (println "cleanup-restart"))
      (give (v) v))))

; define-condition hierarchy + condition-of-type? + type-pred
(define-condition 'file-error 'error)
(define-condition 'not-found 'file-error)
(println "subtype-self:" (condition-of-type? {:type 'file-error} 'file-error))
(println "subtype-parent:" (condition-of-type? {:type 'file-error} 'error))
(println "subtype-grand:" (condition-of-type? {:type 'not-found} 'error))
(println "subtype-reverse:" (condition-of-type? {:type 'error} 'file-error))
(println "subtype-unrelated:" (condition-of-type? {:type 'other} 'error))
(println "catch-subtype:"
  (try (error {:type 'not-found :message "missing.txt"})
    (catch ((type-pred 'error) e) (condition-message e))))
(println "pred-specific:"
  (try (error "plain")
    (catch ((type-pred 'file-error) e) "wrong-clause")
    (catch ((type-pred 'error) e) "right-clause")))
