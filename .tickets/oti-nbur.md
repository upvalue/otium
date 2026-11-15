---
id: oti-nbur
status: open
deps: []
links: []
created: 2026-11-10T04:16:09Z
type: task
priority: 3
assignee: Phil
parent: oti-gwlk
tags: [threads, actors, design]
---
# Strawman: mailbox limits and slowing down senders

STRAWMAN FOR DISCUSSION. Mailboxes grow without limit and send never waits, so a slow receiver cannot slow its senders. The mailbox is also the only way to wait on several things at once. Messages are passed by reference, so two actors can mutate the same value.

## Design

Questions: 1. Optional per-thread mailbox limit, with send parking (or returning #f) when full? Parking senders can deadlock actor pairs; Erlang chose no limit. 2. Is a separate bounded queue type wanted, or is the mailbox enough? 3. Messages by reference: leave alone, or freeze/check in debug builds (open question 6 in scratch/green-threads.md)?

## Acceptance Criteria

Decisions recorded; follow-ups filed.

