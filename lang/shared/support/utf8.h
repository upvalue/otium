#ifndef OTIUM_SUPPORT_UTF8_H
#define OTIUM_SUPPORT_UTF8_H
#include <stddef.h>

/* Validate a borrowed byte span before converting foreign data to a string.
 * Accepts empty spans and U+0000; rejects overlong encodings, surrogates,
 * truncated sequences and values beyond U+10FFFF. Does not read past size.
 * Callers enforce their own NUL/framing rules. No allocation or retained state. */
int ot_utf8_valid(const unsigned char* bytes, size_t size);
#endif
