#ifndef OTIUM_SLJIT_CONFIG_H
#define OTIUM_SLJIT_CONFIG_H

/* How the runtime builds sljit: native.c and sljit.c both include this before
 * sljitLir.h, so they agree. Executable memory comes from the runtime's code
 * space (oti_code_space_alloc in native.c), not sljit's allocator. */
#include <stddef.h>

#define SLJIT_CONFIG_AUTO 1
#define SLJIT_EXECUTABLE_ALLOCATOR 0
#define SLJIT_MALLOC_EXEC(size, data) oti_code_space_alloc((data), (size))
#define SLJIT_FREE_EXEC(ptr, data) oti_code_space_free((data), (ptr))
#ifdef OTIUM_SLJIT_DEBUG
#define SLJIT_DEBUG 1
#define SLJIT_VERBOSE 1
#else
#define SLJIT_DEBUG 0
#define SLJIT_VERBOSE 1
#endif

void* oti_code_space_alloc(void* space, size_t size);
void oti_code_space_free(void* space, void* code);

#endif
