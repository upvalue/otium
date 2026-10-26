#!/usr/bin/env python3
"""Compare signal-site ordering, masking and error propagation with Chez."""
import differential

PREFIX = '(def events []) (def (record x) (push! events x)) (def (yes _) #t) (defparam p :default) '
differential.CASES = [PREFIX + source for source in [
    "(println (signal 'unhandled))",
    '(println (handler-bind () 42))',
    '(handler-bind ((yes (fn (c) (record :outer)))) (handler-bind ((yes (fn (c) (record :one))) (yes (fn (c) (record :two) (signal :nested)))) (signal :original))) (println events)',
    '(handler-bind ((yes (fn (c) (record c)))) (try (handler-bind ((yes (fn (c) #f))) (try (error :once) (catch ((fn (_) #f) c) :wrong))) (catch c #f))) (println events)',
    '(handler-bind ((yes (fn (c) (record :outer)))) (handler-bind (((begin (record :pred) (signal :init) yes) (begin (record :handler) (fn (c) (record :inner))))) (signal :body))) (println events)',
    '(handler-bind ((yes (fn (c) (record (p))))) (with-params ((p :site)) (try (car 42) (catch c :caught)))) (println events)',
    '(try (handler-bind ((yes (fn (c) (record :old) (error :replacement)))) (error :original)) (catch c (record c))) (println events)',
    '(handler-bind ((yes (fn (c) (record :outer)))) (try (handler-bind ((yes car)) (signal 1)) (catch c :caught))) (println events)',
    '(handler-bind ((yes (fn (c) (record :outer)))) (handler-bind ((yes (fn (c) (handler-bind ((yes (fn (d) (record :new)))) (signal :nested))))) (signal :original))) (println events)',
    "(handler-bind ((yes (fn (c) (record c)))) (eval '(signal :eval))) (println events)",
    '(handler-bind ((yes (fn (c) (record c)))) (signal #f) (signal nil)) (println events)',
    '(handler-bind ((yes (fn (c) (record (list (condition-type c) (condition-message c) (get c :data)))))) (signal "message" 1 2)) (println events)',
    '(handler-bind ((yes (fn (c) (record :outer)))) (handler-bind (((fn (c) (signal :predicate) #f) (fn (c) (record :wrong)))) (signal :body))) (println events)',
    '(handler-bind ((yes (fn (c) (record :outer)))) (try (handler-bind ((1 yes)) (signal :body)) (catch c :caught))) (println events)',
]]
differential.INVALID = ['(handler-bind)', '(handler-bind ((1)))', '(handler-bind ((1 2 3)))', '(signal)', '(signal :x 1)']
if __name__ == '__main__':
    differential.main()
