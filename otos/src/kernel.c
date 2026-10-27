#include "otium.h"
#include "kernel.h"
#include "line.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Each semispace starts at 1 MB and can grow to 16 MB. */
#define HEAP_INITIAL (1024u * 1024u)
#define HEAP_LIMIT (16u * 1024u * 1024u)
/* VM value slots. */
#define STACK_LIMIT (128u * 1024u)

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

[[noreturn]] void kernel_fault(uintptr_t cause, uintptr_t pc, uintptr_t value) {
  printf("otos: trap cause=%#lx pc=%#lx value=%#lx\n", (unsigned long)cause, (unsigned long)pc,
         (unsigned long)value);
  sbi_shutdown(true);
}

/* Evaluates one form and prints its value or error. Returns false when the
 * form asked to exit. */
static bool evaluate(ot_state* rt, const char* source, size_t length, int* status) {
  ot_value result;
  console_interrupt = 0;
  console_evaluating = true;
  bool ok = ot_eval(rt, source, length, &result);
  console_evaluating = false;
  if (ot_exit_status(rt, status)) return false;
  if (ok) {
    ot_write(rt, stdout, result);
    putchar('\n');
  } else if (ot_interrupted(rt)) {
    puts("interrupted");
  } else {
    printf("error: %s\n", ot_error(rt));
  }
  return true;
}

/* Returns the exit status: from (exit n), or 0 for ^D. */
static int repl(ot_state* rt) {
  static char line[1024];
  char* pending = NULL;
  size_t length = 0;
  int status = 0;
  for (;;) {
    int read = line_read(length == 0 ? "ot> " : "..> ", line, sizeof line);
    if (read == LINE_EOF) break;
    if (read == LINE_INTERRUPTED) {
      length = 0;
      continue;
    }
    line_history_add(line, read);
    char* grown = realloc(pending, length + (size_t)read + 1);
    if (grown == NULL) {
      puts("otos: out of memory");
      length = 0;
      continue;
    }
    pending = grown;
    memcpy(pending + length, line, (size_t)read);
    length += (size_t)read;
    pending[length++] = '\n';

    /* Evaluate each complete form; keep an incomplete one for the next line. */
    size_t consumed = 0;
    for (;;) {
      size_t end, error_offset;
      const char* error;
      ot_form_status form = ot_scan_form(pending, length, consumed, &end, &error, &error_offset);
      if (form == OT_FORM_COMPLETE) {
        if (!evaluate(rt, pending + consumed, end - consumed, &status)) {
          free(pending);
          return status;
        }
        consumed = end;
        continue;
      }
      if (form == OT_FORM_END) consumed = length;
      if (form == OT_FORM_ERROR) {
        printf("error: %s\n", error);
        consumed = length;
      }
      break;
    }
    memmove(pending, pending + consumed, length - consumed);
    length -= consumed;
  }
  free(pending);
  return status;
}

[[noreturn]] void kmain(uintptr_t hart, uintptr_t device_tree) {
  printf("otos: hart %lu, device tree at %#lx\n", (unsigned long)hart,
         (unsigned long)device_tree);

  console_init();
  uint64_t start = kernel_uptime_ns();
  ot_state* rt = ot_create(HEAP_INITIAL, STACK_LIMIT);
  if (rt == NULL) {
    puts("otos: ot_create failed");
    sbi_shutdown(true);
  }
  /* The I/O host has to be in place before the core library loads, since the
   * runtime reads its library files and standard ports through it. */
  if (!ot_set_heap_limit(rt, HEAP_LIMIT) || !ot_set_io_host(rt, &kernel_io_host, NULL) ||
      !ot_load_expander(rt, "/lib") || !ot_load_core(rt, "/lib") || !gfx_register(rt)) {
    printf("otos: cannot start the runtime: %s\n", ot_error(rt));
    sbi_shutdown(true);
  }
  ot_set_interrupt_flag(rt, &console_interrupt);
  printf("otos: runtime ready in %lu ms\n",
         (unsigned long)((kernel_uptime_ns() - start) / 1000000));

  puts("otium repl (otos) - ^C interrupts, ^D shuts down");
  int status = repl(rt);
  sbi_shutdown(status != 0);
}
