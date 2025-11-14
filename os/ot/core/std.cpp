#include "ot/common.h"

#if !defined(OT_ARCH_WASM) && !defined(OT_POSIX)
int oputsn(const char *str, int n) {
  for (int i = 0; i < n; i++) {
    oputchar(str[i]);
  }
  return 1;
}

// C++ runtime stubs for freestanding environment
extern "C" {
// Global destructor registration (we don't support dynamic globals destruction)
int __cxa_atexit(void (*)(void *), void *, void *) { return 0; }

// Pure virtual function call handler
void __cxa_pure_virtual() {
  oprintf("Pure virtual function called!\n");
  while (1) {
  }
}
}

// Placement new operator (for constructing objects in pre-allocated memory)
void *operator new(size_t, void *ptr) noexcept { return ptr; }

// Operator delete stubs (we don't use dynamic allocation for graphics classes)
void operator delete(void *) noexcept {}
void operator delete(void *, unsigned int) noexcept {}
void operator delete(void *, unsigned long) noexcept {}

#endif // !OT_ARCH_WASM && !OT_POSIX
