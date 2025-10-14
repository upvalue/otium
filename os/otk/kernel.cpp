#include "otcommon.h"

extern "C" char __bss[], __bss_end[], __stack_top[];

void *memset(void *buf, char c, size_t n) {
  uint8_t *p = (uint8_t *)buf;
  while (n--)
    *p++ = c;
  return buf;
}

extern "C" void kernel_main(void) {
  /*
  memset(__bss, 0, (size_t)__bss_end - (size_t)__bss);
  const char *s = "\n\nHello World!\n";
  for (int i = 0; s[i] != '\0'; i++) {
    putchar(s[i]);
  }*/

  putchar('X');
  for (;;) {
    __asm__ __volatile__("wfi");
  }
}
