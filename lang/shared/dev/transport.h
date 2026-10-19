#ifndef OTIUM_DEV_TRANSPORT_H
#define OTIUM_DEV_TRANSPORT_H
#include <stdint.h>
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
const char* ot_dev_default_path(void);
int ot_dev_pid(void);
const char* ot_dev_cwd(void);
int64_t ot_dev_now(void);
const char* ot_dev_realpath(const char* path);
int64_t ot_dev_mtime(const char* path);
#endif
