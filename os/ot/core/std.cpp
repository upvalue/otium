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

// Operator delete implementations (declared in common.h)
// We don't use dynamic allocation in freestanding environment
void operator delete(void *) noexcept {}
void operator delete(void *, unsigned int) noexcept {}
void operator delete(void *, unsigned long) noexcept {}

#endif // !OT_ARCH_WASM && !OT_POSIX
