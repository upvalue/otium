#!/usr/bin/env python3
"""Compare dynamic restart lookup and transfers with Chez."""
import differential

differential.CASES = [
    "(println (restart-case (+ 1 2) (give (v) v)))",
    "(println (handler-bind ((error? (fn (e) (invoke-restart 'give 42)))) (restart-case (+ 1000 (error \"bad\")) (give (v) v))))",
    "(println (restart-case (restart-case (invoke-restart 'pick) (pick () :inner)) (pick () :outer)))",
    "(println (restart-case (let ((r (find-restart 'pick))) (restart-case (invoke-restart r) (pick () :inner))) (pick () :outer)))",
    "(println (restart-case (invoke-restart :give 9) (give (v) v)))",
    "(println (restart-case (invoke-restart \"give\" 10) (give (v) v)))",
    "(println (restart-case (invoke-restart 'pick) (pick () :first) (pick () :last)))",
    "(println (restart-case (map restart-name (compute-restarts)) (a () 1) (b () 2)))",
    "(println (restart-case (restart-description (find-restart 'give)) (give \"A value\" (v) v)))",
    "(println (restart-case (restart-description (find-restart 'give)) (give (v) v)))",
    "(def saved nil) (restart-case (set! saved (find-restart 'give)) (give (v) v)) (println (find-restart saved))",
    "(println (restart-case (invoke-restart 'again) (again () (eq? (find-restart 'again) (get (compute-restarts) 0)))))",
    "(println (restart-case (invoke-restart 'again 0) (again (n) (if (= n 10000) n (invoke-restart 'again (+ n 1))))))",
    "(println (restart-case (invoke-restart 'first) (first () (invoke-restart 'second)) (second () :done)))",
    "(defparam p :default) (println (with-params ((p :outer)) (restart-case (with-params ((p :inner)) (invoke-restart 'give)) (give () (p)))))",
    "(println (restart-case (try (invoke-restart 'give 42) (catch e 0)) (give (v) v)))",
    "(println (restart-case (eval '(invoke-restart 'give 42)) (give (v) v)))",
]
differential.INVALID = ['(restart-case)', '(find-restart 42)', '(restart-name 1)', '(restart-description 1)', '(compute-restarts 1)', '(invoke-restart)', "(invoke-restart 'missing)", "(restart-case (invoke-restart 'give) (give (v) v))"]
if __name__ == '__main__':
    differential.main()
