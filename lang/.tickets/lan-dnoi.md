---
id: lan-dnoi
status: closed
deps: []
links: []
created: 2026-08-15T23:20:34Z
type: bug
priority: 2
assignee: Phil
---
# string nchars not computed for multibyte UTF-8

Conformance 10-library fails: (string-length "héllo") returns 6 (bytes), spec wants 5 (code points). make_string_h sets nchars = len with a comment that the caller/reader may fix up for multibyte, but neither the reader nor make_string ever does. Fix: count code points in make_string_h (or a fixup pass in make_string) so nchars is always correct; audit substring/indexing paths that trust nchars.

## Acceptance Criteria

tests/otium/run-tests.py: 10-library passes; (string-length "héllo") is 5


## Notes

**2026-08-16T00:22:50Z**

Fixed 2026-08-15 during lan-c7gk: make_string_h and make_string_from_h now count code points (utf8_count) at construction, so nchars is correct for every string. 10-library passes (normal and gc_stress builds); (string-length "héllo") = 5.
