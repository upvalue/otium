#ifndef OTIUM_LINE_EDIT_H
#define OTIUM_LINE_EDIT_H

/* The returned UTF-8 string belongs to the editor until the next read or close.
 * NULL means EOF; an interrupted read returns an empty string. */
const char* ot_line_read(const char* prompt);
int ot_line_interactive(void);
void ot_line_set_service(void (*service)(void *), void *userdata);
int ot_line_interrupted(void);
void ot_line_close(void);

/* Copy a read-only snapshot of visible bindings before reading a prompt. */
void ot_line_bindings_clear(void);
void ot_line_binding(const char* name, const char* documentation);

#endif
