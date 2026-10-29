#ifndef OTIUM_DEV_TRANSPORT_H
#define OTIUM_DEV_TRANSPORT_H
#include <stdint.h>
#include <stddef.h>
/* One process-wide server, serviced on the interpreter thread. No managed
 * pointers cross this boundary. Calls are not thread-safe or reentrant.
 * open borrows path for the call; returns 0 or -1. close releases all queues
 * and removes the socket only if its inode still matches; repeated close is OK. */
int ot_dev_open(const char* path);
void ot_dev_close(void);
/* Poll for at most timeout milliseconds (-1 waits indefinitely); returns
 * whether a complete request is queued. Invalid frames disconnect the client.
 * Payloads must be valid UTF-8 without NUL and at most 4 MiB. */
int ot_dev_poll(int timeout);
/* Flattened fd/event pairs, events 1=read and 2=write. Returns slots needed. */
size_t ot_dev_fds(int* pairs, size_t capacity);
/* Borrowed string, valid until next/close; NULL means the queue is empty.
 * client returns the ID of the last dequeued request. Copy before polling the
 * next request; the interpreter owns evaluation and response policy. */
const char* ot_dev_next(void);
int ot_dev_client(void);
/* Copies text during the call. ID 0 broadcasts; other IDs address one client.
 * Oversized output, queue overflow or IO failure disconnects affected clients. */
void ot_dev_send(int client, const char* text);
/* Monotonic milliseconds, for elapsed-time comparisons only. */
uint64_t ot_dev_clock(void);
/* Effective UID and a POSIX private-directory check, with no path policy.
 * The latter creates with mode 0700 if missing, then rejects symlinks, foreign
 * owners and group/other permissions. Returns 1 on success, 0 on failure. */
int ot_dev_uid(void);
int ot_dev_private_directory(const char* path);
int ot_dev_pid(void);
/* cwd uses static storage; realpath uses static storage on success and returns
 * the supplied path on failure. Copy either result before another call. */
const char* ot_dev_cwd(void);
/* Wall-clock milliseconds since the Unix epoch. */
int64_t ot_dev_now(void);
const char* ot_dev_realpath(const char* path);
/* File modification time in epoch milliseconds, or -1 when stat fails. */
int64_t ot_dev_mtime(const char* path);
#endif
