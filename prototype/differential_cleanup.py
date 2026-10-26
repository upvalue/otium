#!/usr/bin/env python3
"""Compare cleanup ordering, outcomes and replacement transfers with Chez."""
import differential
import tempfile
from pathlib import Path

SETUP = '(def events []) (def (record x) (push! events x)) '
differential.CASES = [SETUP + body for body in [
    '(println (unwind-protect 42 (record :normal)) events)',
    "(try (unwind-protect (unwind-protect (error :body) (record :inner)) (record :outer)) (catch e (record e))) (println events)",
    "(println (try (unwind-protect (error :body) (error :cleanup)) (catch e e)))",
    "(println (try (restart-case (unwind-protect (invoke-restart 'done) (error :cleanup)) (done () :wrong)) (catch e e)))",
    "(println (restart-case (eval '(unwind-protect (invoke-restart 'done 42) (record :eval))) (done (v) v)) events)",
    "(println (handler-bind ((error? (fn (c) (unwind-protect (invoke-restart 'done 42) (record :handler))))) (restart-case (unwind-protect (error \"bad\") (record :body)) (done (v) v))) events)",
    "(defparam p :default) (with-params ((p :outer)) (try (unwind-protect (with-params ((p :inner)) (error :bad)) (record (p))) (catch e nil))) (println events)",
    "(try (unwind-protect (try (error :inner) (catch e 42)) (record :once)) (catch e nil)) (println events)",
    "(println (call-with-cleanup (fn () 42) (fn (o) (record o))) events)",
    "(try (call-with-cleanup (fn () (error :body)) (fn (o) (record o) (error :release))) (catch e (record e))) (println events)",
    "(println (try (call-with-cleanup (fn () 42) (fn (o) (error :release))) (catch e e)))",
    "(println (restart-case (call-with-cleanup (fn () (invoke-restart 'done 42)) (fn (o) (record o) (error :release))) (done (v) v)) events)",
    "(println (with ((a 1 record) (b (+ a 1) record)) 42) events)",
    "(println (restart-case (unwind-protect (error :body) (invoke-restart 'done 42)) (done (v) v)))",
    "(println (try (unwind-protect (exit 7) (error :cleanup)) (catch e e)))",
    "(println (defer 42))",
]]
differential.INVALID = ['(unwind-protect)', '(defer)', '(exit 256)', '(exit -1)', '(exit 1.0)', '(exit 1 2)', '(quit "1")', '(call-with-cleanup (fn () 42) 1)']
# specs/io.md preserves a pending transfer when resource cleanup raises.
# Chez currently replaces it. Keep explicit assertions for both behaviors.
SPEC_DIFFERENCES = [
    (differential.CASES[9], '[:body :body]\n', '[:body :release]\n'),
    (SETUP + "(println (try (restart-case (call-with-cleanup (fn () (invoke-restart 'done 42)) (fn (o) (record o) (error :release))) (done (v) v)) (catch e e)) events)", '42 [nil]\n', ':release [nil]\n'),
]
differential.CASES = [case for i, case in enumerate(differential.CASES) if i not in (9, 11)]
if __name__ == '__main__':
    differential.main()
    with tempfile.TemporaryDirectory() as directory:
        path = Path(directory) / 'cleanup.ot'
        for source, prototype, chez in SPEC_DIFFERENCES:
            path.write_text(source)
            for command, expected in [([differential.ROOT / 'prototype/otium-proto'], prototype),
                                      ([differential.ROOT / 'otium', '--runtime=chez', '--no-project'], chez)]:
                result = differential.run(command, path)
                assert result.returncode == 0 and result.stdout == expected, result
    print('Verified 2 documented resource cleanup differences')
