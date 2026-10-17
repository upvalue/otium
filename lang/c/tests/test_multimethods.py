#!/usr/bin/env python3
"""Check wildcard resolution against a bitset oracle and namespace integration."""
import os
from pathlib import Path
import subprocess
import sys
import tempfile

binary = str(Path(sys.argv[1]).resolve())
forms = ["(require (otium.generic :as g :refer (Number)))", "(defgeneric (f a b c))"]
expected = []
for subset in range(256):
    masks = [m for m in range(8) if subset & (1 << m)]
    best = [m for m in masks if not any(m != n and (m & n) == m for n in masks)]
    answer = "missing-method" if not best else "ambiguous-method" if len(best) > 1 else str(best[0])
    for order in (masks, list(reversed(masks))):
        forms.append("(reset-generic! (f a b c))")
        for mask in order:
            parameters = " ".join(f"({p} Number)" if mask & (1 << i) else p for i, p in enumerate("abc"))
            forms.append(f"(defmethod (f {parameters}) {mask})")
        forms.append("(println (try (f 1 2 3) (catch (error? e) (get e :code))))")
        expected.append(answer)

with tempfile.TemporaryDirectory(prefix="otium-multimethods-") as directory:
    root = Path(directory)
    (root / "provider.ot").write_text("""
(ns provider)
(require (otium.generic :as g))
(defclass Animal value)
(defgeneric (act a b))
(defmethod (act (a Animal) b) 'animal)
""")
    forms.extend(["""
(require (provider :as p :refer (act)))
(defclass Animal value)
(def animal (g/make p/Animal :value 1))
(def saved act)
(println (eq? Animal p/Animal))
(defmethod (p/act a (b Number)) 'number)
(println (try (act animal 1) (catch (error? e) (get e :code))))
(defmethod (p/act (a p/Animal) (b Number)) 'exact)
(println (saved animal 1))
(require (provider :as p :reload))
(println (eq? saved p/act))
(println (eq? (g/dispatch-type animal) p/Animal))
(println (act animal 1))
(println (try (eval '(defclass act)) (catch (error? e) (get e :code))))
(println (try (eval '(reset-generic! (act a b))) (catch (error? e) (get e :code))))
(println (g/remove-method! act (list p/Animal Number)))
(println (try (act animal 1) (catch (error? e) (get e :code))))
(println (g/remove-method! act (list p/Animal #f)))
(println (act animal 1))
(ns provider)
(reset-generic! (act a))
(ns user)
(println (eq? saved p/act))
(println (try (p/act animal 1) (catch (error? e) (get e :code))))
(println (saved animal 1))
"""])
    expected.extend(["#f", "ambiguous-method", "exact", "#t", "#t", "exact",
                     "binding-conflict", "binding-conflict", "#t", "ambiguous-method",
                     "#t", "number", "#f", "wrong-arity", "number"])
    path = root / "checks.ot"
    path.write_text("\n".join(forms))
    result = subprocess.run([binary, "--no-project", "--path", directory, str(path)],
                            text=True, capture_output=True, timeout=180, env=os.environ.copy())
    if result.returncode or result.stdout.splitlines() != expected:
        print(result.stderr, file=sys.stderr)
        actual = result.stdout.splitlines()
        for i, (want, got) in enumerate(zip(expected, actual)):
            if want != got:
                print(f"check {i}: expected {want}, got {got}", file=sys.stderr)
                break
        print(f"exit {result.returncode}; expected {len(expected)} results, got {len(actual)}", file=sys.stderr)
        sys.exit(1)
print(f"{len(expected)} multimethod oracle/namespace checks passed")
