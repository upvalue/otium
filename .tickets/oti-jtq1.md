---
id: oti-jtq1
status: open
deps: []
links: []
created: 2026-11-10T04:16:13Z
type: task
priority: 3
assignee: Phil
parent: oti-gwlk
tags: [threads, portability, design]
---
# Decide whether threads are native-only

STRAWMAN FOR DISCUSSION. scratch/green-threads.md listed otos and the VM as goals. The implementation needs native code, a pthread ticker and poll(); on the VM every thread builtin raises. Either say threads are native-only (and update the notes), or plan the VM port: per-thread exec already exists, preemption would be a countdown at the VM's safe points, and waits need the VM's C nesting removed first.

