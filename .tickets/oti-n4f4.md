---
id: oti-n4f4
status: open
deps: []
links: []
created: 2026-11-10T04:16:08Z
type: task
priority: 2
assignee: Phil
parent: oti-gwlk
tags: [threads, actors, design]
---
# Strawman: failure and lifetime rules for spawned threads

STRAWMAN FOR DISCUSSION. Today: a plain spawn that fails only prints to stderr; the main thread ending exits the process without waiting; groups (kill siblings, interrupt owner) are opt-in through thread/group and spawn-in; join on a killed thread returns the kill reason as if it were a result, so a thread that returns :killed and one that was killed look the same.

## Design

Options: 1. Erlang style: add links, and supervisors written in Otium on monitors; spawn stays detached. 2. Structured by default: spawn belongs to the enclosing group and a detached form is the opt-out. 3. Keep as is and only fix join's result for killed threads (raise a thread-killed condition, or return a distinct value). Also decide whether a failure that a joiner, group or monitor receives should still print.

## Acceptance Criteria

Phil has picked the rules; follow-up tickets filed for the work.

