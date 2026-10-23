#define OT_INTERNAL
#include "otium.h"
#include "kernel.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

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

/* Reads one line from the console into line, echoing as it goes. Returns the
 * line's length, LINE_INTERRUPTED for ^C, or LINE_EOF for ^D on an empty line. */
#define LINE_EOF -1
#define LINE_INTERRUPTED -2

static int read_line(char* line, int capacity) {
  int length = 0;
  for (;;) {
    int c = console_read_byte();
    if (c == '\r' || c == '\n') {
      console_write("\n", 1);
      return length;
    } else if (c == 0x03) {
      console_write("^C\n", 3);
      return LINE_INTERRUPTED;
    } else if (c == 0x04) {
      if (length == 0) return LINE_EOF;
    } else if (c == 0x7f || c == 0x08) {
      /* Drop a whole UTF-8 sequence: continuation bytes, then the lead byte. */
      while (length > 0 && ((unsigned char)line[length - 1] & 0xc0) == 0x80) length--;
      if (length > 0) {
        length--;
        console_write("\b \b", 3);
      }
    } else if ((c >= 0x20 || c == '\t') && length < capacity) {
      line[length++] = (char)c;
      char byte = (char)c;
      console_write(&byte, 1);
    }
  }
}

static void report_condition(ots* state) {
  otv condition = ot_condition(state);
  OT_FRAME_SCOPED(state, &condition);
  ot_clear_condition(state);
  fputs("error: ", stdout);
  if (ot_has_type(condition, OBJ_TABLE)) {
    otv key = ot_intern(state, "message", 7, true);
    otv message = ot_table_get(state, condition, key, ot_nil);
    const char* text;
    size_t length;
    if (ot_string_bytes(message, &text, &length)) {
      console_write(text, length);
      console_write("\n", 1);
      return;
    }
  }
  ot_repr_to(state, condition, false, write_console, NULL);
  console_write("\n", 1);
}

static void repl(ots* state) {
  static char line[1024];
  char* pending = NULL;
  size_t pending_length = 0;
  for (;;) {
    fputs(pending_length == 0 ? "ot> " : "..> ", stdout);
    int length = read_line(line, sizeof line);
    if (length == LINE_EOF) break;
    if (length == LINE_INTERRUPTED) {
      pending_length = 0;
      continue;
    }
    char* grown = realloc(pending, pending_length + (size_t)length + 1);
    if (grown == NULL) {
      puts("otos: out of memory");
      pending_length = 0;
      continue;
    }
    pending = grown;
    memcpy(pending + pending_length, line, (size_t)length);
    pending_length += (size_t)length;
    pending[pending_length++] = '\n';

    size_t consumed;
    bool incomplete;
    otv result;
    if (ot_eval_partial(state, pending, pending_length, "<repl>", &consumed, &incomplete,
                        &result)) {
      OT_FRAME_SCOPED(state, &result);
      ot_repr_to(state, result, false, write_console, NULL);
      console_write("\n", 1);
      pending_length = 0;
      continue;
    }
    if (consumed != 0) {
      memmove(pending, pending + consumed, pending_length - consumed);
      pending_length -= consumed;
    }
    if (incomplete) {
      ot_clear_condition(state);
      continue;
    }
    report_condition(state);
    pending_length = 0;
  }
  free(pending);
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

  puts("otium repl (otos) - ^C clears the line, ^D shuts down");
  repl(state);
  sbi_shutdown(false);
}
