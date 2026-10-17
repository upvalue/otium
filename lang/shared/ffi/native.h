#ifndef OTIUM_FFI_NATIVE_H
#define OTIUM_FFI_NATIVE_H
#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>
enum {
  F_VOID,
  F_BOOL,
  F_I8,
  F_U8,
  F_I16,
  F_U16,
  F_I32,
  F_U32,
  F_I64,
  F_U64,
  F_SHORT,
  F_USHORT,
  F_INT,
  F_UINT,
  F_LONG,
  F_ULONG,
  F_SIZE,
  F_DIFF,
  F_FLOAT,
  F_DOUBLE,
  F_POINTER,
  F_CALLBACK,
  F_UTF8,
  F_COUNT
};
extern const char* const offi_names[F_COUNT];
size_t offi_size(int type);
size_t offi_align(int type);
int offi_signed(int type);
void* offi_open(const char* path);
void* offi_symbol(void* library, const char* name);
const char* offi_loader_error(void);
int offi_utf8(const unsigned char* bytes, size_t size);
#endif
