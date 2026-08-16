---
id: lan-goky
status: open
deps: []
links: [lan-2488, lan-myh9, lan-ueba]
created: 2026-08-16T20:27:02Z
type: bug
priority: 0
assignee: Phil
tags: [embedded, heap, alloc, bug]
---
# Code bytes and consts are malloc'd but freed through the allocator seam

make_code allocates code->bytes (src/code.c:44) and code->consts (src/code.c:50) with raw malloc, but both are released with ot_free -- in the collectInto sweep (src/heap.c:221-222) and in heap_deinit (src/heap.c:56-57). With the default malloc-backed allocator this is invisible. Any embedded host that installs its own OtAllocator hands a foreign pointer to its own free on the first collection that reaps a Code object.

Found by building a counting allocator against libotium.a and installing it via ot_set_allocator: the process aborts with "pointer being freed was not allocated". Tolerating foreign pointers to let the run finish counted 1521 of them in one short session -- two per Code object, and the prelude/expander bootstrap alone compiles hundreds.

src/code.c:74 (code_verify boundaries table) uses calloc/free. That pair is internally consistent so it does not corrupt anything, but it bypasses the seam and so escapes any host memory accounting or arena.

Fix: route all three through ot_alloc/ot_free. Then add a guard so it cannot regress -- src/vec.c is the only file in src/ that should name malloc/realloc/free directly. A clang-tidy check or a grep in CI both work; .clang-tidy already exists.

## Acceptance Criteria

No raw malloc/calloc/realloc/free in src/ outside vec.c backend functions. A State created under a non-malloc OtAllocator survives repeated compile-and-collect cycles with no foreign pointer reaching the host free.


## Notes

**2026-08-16T21:39:23Z**

Done. Code allocs are gone entirely (inline storage); verifier table uses ot_alloc/ot_free. check_hygiene.py bans raw malloc in src/ outside vec.c.
