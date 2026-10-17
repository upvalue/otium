---
id: lan-9kz8
status: closed
deps: []
links: []
created: 2026-10-17T22:12:09Z
type: task
priority: 2
assignee: Phil
tags: [portability, design]
---
# Make Chez arithmetic follow the spec for division, rounding and sqrt

Shared code gets different number types back on C and Chez, so it can take different branches (int?, exact?, = against a float). Checked 2026-10-17:

| Expression | C | Chez |
| --- | --- | --- |
| (/ 7 2) | 3.5 | 7/2 |
| (round 2.5), (round -2.5) | 3, -3 | 2.0, -2.0 |
| (floor 2.7) | 2 (int) | 2.0 (float) |
| (sqrt 4) | 2.0 | 2 (exact) |
| (sqrt -1) | nan | 0+1i |
| (= (/ 1 3) 0.3333333333333333) | #t | #f |
| (* 99999999999 99999999999) | 1864711849423024129 (wraps) | 9999999999800000000001 |

C matches specs/language.md 10.1. GAPS.txt currently lists the Chez numeric tower as an intentional difference, so this reverses that.

Proposal: on Chez, Otium-level /, round, floor, ceiling, truncate and sqrt follow the spec (int division stays int only when exact, else float; rounding returns ints, round is half away from zero; sqrt always returns a float, nan for negatives). Hot code can still use Chez fl/fx ops directly.

Overflow (wrap vs bignum) is harder and left out here: wrapping on Chez costs every int op, bignums on C cost a lot of code, erroring on both adds checks on C. Decide separately.

## Acceptance Criteria

The table above gives the same results on both runtimes except overflow; GAPS.txt and docs/portability.md updated; shared tests cover each operation.


## Notes

**2026-10-17T22:14:29Z**

Implemented Otium wrappers for Chez division, floor, ceiling, truncate, round, and sqrt. Shared arithmetic fixture checks values, types, fractional division order, half-away rounding, range errors, and negative square roots (181 checks per runtime). Native Chez operations and bignums remain available. Updated GAPS.txt and docs/portability.md. Validation: make test-library test-chez passed, including C GC-stress library fixtures. No C allocation or managed-pointer code changed.
