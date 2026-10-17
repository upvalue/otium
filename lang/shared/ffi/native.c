#include "native.h"
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
#define TYPES(X)                                                                                   \
  X(F_BOOL, bool)                                                                                  \
  X(F_I8, int8_t) X(F_U8, uint8_t) X(F_I16, int16_t) X(F_U16, uint16_t) X(F_I32, int32_t)          \
      X(F_U32, uint32_t) X(F_I64, int64_t) X(F_U64, uint64_t) X(F_SHORT, short)                    \
          X(F_USHORT, unsigned short) X(F_INT, int) X(F_UINT, unsigned int) X(F_LONG, long)        \
              X(F_ULONG, unsigned long) X(F_SIZE, size_t) X(F_DIFF, ptrdiff_t) X(F_FLOAT, float)   \
                  X(F_DOUBLE, double) X(F_POINTER, void*) X(F_CALLBACK, void (*)(void))            \
                      X(F_UTF8, char*)
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
int offi_utf8(const unsigned char* p, size_t n) {
  for (size_t i = 0; i < n;) {
    unsigned c = p[i++];
    if (c < 128) continue;
    unsigned count, min, v;
    if (c >= 0xc2 && c <= 0xdf) {
      count = 1;
      min = 0x80;
      v = c & 31;
    } else if (c >= 0xe0 && c <= 0xef) {
      count = 2;
      min = 0x800;
      v = c & 15;
    } else if (c >= 0xf0 && c <= 0xf4) {
      count = 3;
      min = 0x10000;
      v = c & 7;
    } else return 0;
    if (n - i < count) return 0;
    while (count--) {
      c = p[i++];
      if ((c & 0xc0) != 0x80) return 0;
      v = (v << 6) | (c & 63);
    }
    if (v < min || v > 0x10ffff || (v >= 0xd800 && v <= 0xdfff)) return 0;
  }
  return 1;
}
