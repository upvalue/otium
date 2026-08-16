# Porting notes

- Otium's sequential `let` covers the jobs done by both Scheme `let` and `let*` in these programs. Named lets were translated to local functions.
- Scheme vectors map to Otium arrays. `gcbench` records also map to fixed-layout arrays.
- `destruc` is why Otium gained `set-car!` and `set-cdr!`. Replacing its pairs with arrays would change the thing being measured.
- `quicksort` used an escape continuation only in its untimed result checker. The port uses a boolean loop there; the sorting workload is unchanged.
- The canonical `mperm` input has a very large live set. It needs a much larger heap cap than Otium's 64 MiB default and may still time out in the tree-walking evaluator.
