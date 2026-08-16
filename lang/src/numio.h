// numio.h — the float<->string seam. The printer and reader convert numbers
// only through these two functions, so an embedded host can swap the libc
// snprintf/strtod dependency for a smaller implementation in one place.
#pragma once
#include "common.h"

// Shortest round-trip-ish formatting matching the printer's conventions
// (%.17g fallback narrowed as the C++ printer does). Returns bytes written.
u32 num_format_f64(f64 v, char* out, u32 cap);
u32 num_format_i64(i64 v, char* out, u32 cap);

// Returns true on full parse; *out receives the value.
bool num_parse_f64(const char* s, u32 len, f64* out);
bool num_parse_i64(const char* s, u32 len, i64* out);
