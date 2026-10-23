#ifndef OTOS_LINE_H
#define OTOS_LINE_H

#define LINE_EOF -1
#define LINE_INTERRUPTED -2

/* Reads one line from the console with editing and history. Returns the line's
 * length, LINE_INTERRUPTED for ^C, or LINE_EOF for ^D on an empty line. */
int line_read(const char* prompt, char* line, int capacity);
/* Adds a line to the history that Up and Down walk through. */
void line_history_add(const char* line, int length);

#endif
