#ifndef OTIUM_LINE_EDIT_H
#define OTIUM_LINE_EDIT_H

/* Process-wide Bestline adapter; use from the interpreter thread only.
 * close releases native snapshots/history and allows a later read to restart.
 * The returned UTF-8 string belongs to the editor until the next read or close.
 * NULL means EOF; an interrupted read returns an empty string. */
const char* ot_line_read(const char* prompt);
int ot_line_interactive(void);
/* Borrowed callback/userdata must outlive reads until replaced. Called while
 * waiting for input, never from a signal handler. Must not reenter line input. */
void ot_line_set_service(void (*service)(void*), void* userdata);
int ot_line_interrupted(void);
void ot_line_close(void);

/* Copy a read-only snapshot of visible bindings before reading a prompt. */
void ot_line_bindings_clear(void);
void ot_line_binding(const char* name, const char* documentation);

#endif
