#include "native.h"
#include "../utf8/utf8.h"
#include <dlfcn.h>
#include <stdalign.h>

const char* const offi_names[F_COUNT] = {"void",    "bool",
                                         "i8",      "u8",
                                         "i16",     "u16",
                                         "i32",     "u32",
                                         "i64",     "u64",
                                         "short",   "ushort",
                                         "int",     "uint",
                                         "long",    "ulong",
                                         "size-t",  "ptrdiff-t",
                                         "float",   "double",
                                         "pointer", "callback-pointer",
                                         "utf8"};
// clang-format off
#define TYPES(X) \
  X(F_BOOL, bool)                \
  X(F_I8, int8_t)                \
  X(F_U8, uint8_t)               \
  X(F_I16, int16_t)              \
  X(F_U16, uint16_t)             \
  X(F_I32, int32_t)              \
  X(F_U32, uint32_t)             \
  X(F_I64, int64_t)              \
  X(F_U64, uint64_t)             \
  X(F_SHORT, short)              \
  X(F_USHORT, unsigned short)    \
  X(F_INT, int)                  \
  X(F_UINT, unsigned int)        \
  X(F_LONG, long)                \
  X(F_ULONG, unsigned long)      \
  X(F_SIZE, size_t)              \
  X(F_DIFF, ptrdiff_t)           \
  X(F_FLOAT, float)              \
  X(F_DOUBLE, double)            \
  X(F_POINTER, void*)            \
  X(F_CALLBACK, void (*)(void))  \
  X(F_UTF8, char*)
// clang-format on
size_t offi_size(int type) {
  switch (type) {
#define S(K, T)                                                                                    \
  case K: return sizeof(T);
    TYPES(S)
#undef S
    default: return 0;
  }
}
size_t offi_align(int type) {
  switch (type) {
#define A(K, T)                                                                                    \
  case K: return _Alignof(T);
    TYPES(A)
#undef A
    default: return 0;
  }
}
int offi_signed(int type) {
  return type == F_I8 || type == F_I16 || type == F_I32 || type == F_I64 || type == F_SHORT ||
         type == F_INT || type == F_LONG || type == F_DIFF;
}
void* offi_open(const char* path) {
  dlerror();
  return dlopen(path, RTLD_NOW | RTLD_LOCAL);
}
void* offi_symbol(void* library, const char* name) {
  dlerror();
  return dlsym(library, name);
}
const char* offi_loader_error(void) {
  const char* e = dlerror();
  return e ? e : "loader failure";
}
int offi_utf8(const unsigned char* bytes, size_t size) { return ot_utf8_valid(bytes, size); }
