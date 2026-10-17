# Porting notes

- Otium's sequential `let` covers the jobs done by both Scheme `let` and `let*` in these programs. Named lets were translated to local functions.
- Scheme vectors map to Otium arrays. `gcbench` records also map to fixed-layout arrays.
- `destruc` is why Otium gained `set-car!` and `set-cdr!`. Replacing its pairs with arrays would change the thing being measured.
- `quicksort` used an escape continuation only in its untimed result checker. The port uses a boolean loop there; the sorting workload is unchanged.
- The canonical `mperm` input has a very large live set. It needs a much larger heap cap than Otium's 64 MiB default and may still time out in the tree-walking evaluator.
- `equal` uses structural equality for acyclic Otium arrays in place of Scheme vectors. Its cyclic-list helper is disabled by upstream and is not part of the timed workload.

## Reduced equality check

The reduced `equal` input in `test_ports.py` currently reports `INCORRECT` on
both runtimes. This also reproduces before the repository convention cleanup
(commit `2e04a95`). The runner keeps the failure visible. The other reduced ports
and the optional upstream Guile smoke check pass. Do not treat the aggregate
benchmark correctness target as green until this discrepancy is resolved.
