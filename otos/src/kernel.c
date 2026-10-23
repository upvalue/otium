#define OT_INTERNAL
#include "otium.h"
#include "kernel.h"

#include <stdio.h>
#include <string.h>

static const char program[] =
    "(def (fib n) (if (< n 2) n (+ (fib (- n 1)) (fib (- n 2)))))\n"
    "(fib 25)\n";

static void write_console(void* userdata, const char* bytes, size_t length) {
  (void)userdata;
  console_write(bytes, length);
}

[[noreturn]] void sbi_shutdown(bool failure) {
  /* SBI system reset extension: shutdown, with no reason or a system failure. */
  register uintptr_t a0 __asm__("a0") = 0;
  register uintptr_t a1 __asm__("a1") = failure ? 1 : 0;
  register uintptr_t a6 __asm__("a6") = 0;
  register uintptr_t a7 __asm__("a7") = 0x53525354;
  __asm__ volatile("ecall" : "+r"(a0), "+r"(a1) : "r"(a6), "r"(a7) : "memory");
  for (;;) __asm__ volatile("wfi");
}

/* picolibc's exit and abort end here. */
[[noreturn]] void _exit(int status) { sbi_shutdown(status != 0); }

[[noreturn]] void kernel_trap(uintptr_t cause, uintptr_t pc, uintptr_t value) {
  printf("otos: trap cause=%#lx pc=%#lx value=%#lx\n", (unsigned long)cause, (unsigned long)pc,
         (unsigned long)value);
  sbi_shutdown(true);
}

[[noreturn]] void kmain(uintptr_t hart, uintptr_t device_tree) {
  printf("otos: hart %lu, device tree at %#lx\n", (unsigned long)hart,
         (unsigned long)device_tree);

  uint64_t start = ot_platform_monotonic_ns();
  ots* state = ot_create(NULL);
  if (state == NULL) {
    puts("otos: ot_create failed");
    sbi_shutdown(true);
  }
  uint64_t created = ot_platform_monotonic_ns();
  printf("otos: runtime ready in %lu ms\n", (unsigned long)((created - start) / 1000000));

  otv result;
  bool ok = ot_eval_src(state, program, sizeof program - 1, "<boot>", &result);
  uint64_t finished = ot_platform_monotonic_ns();
  OT_FRAME_SCOPED(state, &result);
  if (!ok) {
    result = ot_condition(state);
    ot_clear_condition(state);
    fputs("otos: error: ", stdout);
  } else {
    fputs("otos: (fib 25) => ", stdout);
  }
  ot_repr_to(state, result, false, write_console, NULL);
  printf("\notos: evaluated in %lu ms\n", (unsigned long)((finished - created) / 1000000));
  sbi_shutdown(!ok);
}
