#include "otk/kernel.hpp"

extern "C" char __bss[], __bss_end[], __stack_top[];

void kernel_common(void) {
  omemset(__bss, 0, (size_t)__bss_end - (size_t)__bss);

  wfi();
}
