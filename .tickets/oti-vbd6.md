---
id: oti-vbd6
status: closed
deps: []
links: []
created: 2026-11-15T23:39:28Z
type: task
priority: 2
assignee: Phil
tags: [manse, linux, runtime]
---
# Manse terminal output on Linux: non-blocking PTY writes

Done in the working tree: manse/pty.ot gets O_RDWR|O_NOCTTY and environ from manse/native/pty.c (was macOS constants and _NSGetEnviron), libc.so.6 candidate. Runtime: terminals opened by open-*-file are O_NONBLOCK; on EAGAIN the writer parks with back-off 1 ms doubling to 32 ms (Linux PTYs poll writable while writes fail for want of buffer space). Before, a full PTY blocked the OS thread and every green thread.

