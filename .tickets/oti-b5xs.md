---
id: oti-b5xs
status: open
deps: []
links: []
created: 2026-10-30T02:22:31Z
type: task
priority: 2
assignee: Phil
tags: [native, threads, memory]
---
# Stop apply from growing every thread's native args array

oti_native_invoke_list (c/src/native.c:1328) grows native.args to the length of the applied list, and oti_native_args then grows every thread's array to match (oti_threads_grow_args, c/src/threads.c:470). New threads are made at that size (threads.c:1051) and arrays never shrink, so one (apply f big-list) sets a permanent per-thread cost. Only compiled call sites (max_args) need to set the shared size; a large apply could grow just the current thread's array and trim it after. Alternative: pass the list straight through when the callee takes a rest parameter.

## Acceptance Criteria

After applying a function to a long list, other threads' and new threads' args arrays are sized by compiled call sites only.

