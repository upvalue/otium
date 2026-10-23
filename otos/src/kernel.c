#define OT_INTERNAL
#include "otium.h"
#include "kernel.h"
#include "line.h"

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

/* Namespaces load from the embedded lib/ tree: otium.json is /lib/otium/json.ot. */
static bool load_namespace(void* userdata, const char* name, char** source, size_t* length) {
  char path[256] = "/lib/";
  size_t used = strlen(path);
  for (const char* cursor = name; *cursor != '\0'; cursor++) {
    if (used + 4 >= sizeof path) return false;
    path[used++] = *cursor == '.' ? '/' : *cursor;
  }
  memcpy(path + used, ".ot", 4);
  if (!ot_platform_read_file(path, source, length)) return false;
  ot_set_source_name(userdata, path);
  return true;
}

static bool poll_console(void* userdata) {
  (void)userdata;
  return console_poll_interrupt();
}

/* The condition being reported, rooted while otium.diagnostics renders it. */
static otv reported_condition;

static otv nat_reported_condition(ots* state, otv* args, int argc) {
  (void)state;
  (void)args;
  (void)argc;
  return reported_condition;
}

static void print_message(ots* state, otv condition) {
  if (ot_has_type(condition, OBJ_TABLE)) {
    otv key = ot_intern(state, "message", 7, true);
    otv message = ot_table_get(state, condition, key, ot_nil);
    const char* text;
    size_t length;
    if (ot_string_bytes(message, &text, &length)) {
      fputs("error: ", stdout);
      console_write(text, length);
      console_write("\n", 1);
      return;
    }
  }
  fputs("error: ", stdout);
  ot_repr_to(state, condition, false, write_console, NULL);
  console_write("\n", 1);
}

/* Renders the condition with otium.diagnostics, as the host REPL does, or
 * prints its message if that fails. */
static void report_condition(ots* state) {
  reported_condition = ot_condition(state);
  ot_clear_condition(state);
  if (ot_value_type(reported_condition) == OT_TYPE_TABLE) {
    const char* code =
        "(begin (require otium.diagnostics) (otium.diagnostics/render (%reported-condition)))";
    otv rendered;
    const char* text;
    size_t length;
    if (ot_eval_src(state, code, strlen(code), "<report>", &rendered) &&
        ot_string_bytes(rendered, &text, &length)) {
      console_write(text, length);
      reported_condition = ot_nil;
      return;
    }
    ot_clear_condition(state);
  }
  print_message(state, reported_condition);
  reported_condition = ot_nil;
}

static void repl(ots* state) {
  static char line[1024];
  char* pending = NULL;
  size_t pending_length = 0;
  for (;;) {
    int length = line_read(pending_length == 0 ? "ot> " : "..> ", line, sizeof line);
    if (length == LINE_EOF) break;
    if (length == LINE_INTERRUPTED) {
      pending_length = 0;
      continue;
    }
    line_history_add(line, length);
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
    pending_length = 0;
    if (state->vm.unwind_kind == UNWIND_QUIT) {
      ot_clear_condition(state);
      if (state->quit_requested) break;
      puts("interrupted");
      continue;
    }
    report_condition(state);
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

  ot_set_loader(state, load_namespace, state);
  state->poll_hook = poll_console;
  ot_global_add(state, &reported_condition);
  ot_def_nat(state, "%reported-condition", nat_reported_condition);

  puts("otium repl (otos) - ^C interrupts, ^D shuts down");
  repl(state);
  sbi_shutdown(state->quit_requested && state->quit_status != 0);
}
