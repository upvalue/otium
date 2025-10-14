#include "otk/kernel.hpp"

static Process procs[PROCS_MAX];

Process *process_create_impl(Process *table, uint32_t max_procs,
                             const char *name, uint32_t pc) {
  Process *free_proc = nullptr;
  uint32_t i;
  for (i = 0; i < max_procs; i++) {
    if (table[i].state == UNUSED) {
      free_proc = &table[i];
      break;
    }
  }

  if (!free_proc) {
    // PANIC("reached process limit");
    return nullptr;
  }

  omemset(free_proc, 0, sizeof(Process));

  for (int j = 0; j < 32; j++) {
    if (!name[j]) {
      break;
    }
    free_proc->name[j] = name[j];
  }

  free_proc->state = RUNNING;
  free_proc->pid = i;

  uint32_t *sp = (uint32_t *)&free_proc->stack[sizeof(free_proc->stack)];
  *--sp = 0;            // s11
  *--sp = 0;            // s10
  *--sp = 0;            // s9
  *--sp = 0;            // s8
  *--sp = 0;            // s7
  *--sp = 0;            // s6
  *--sp = 0;            // s5
  *--sp = 0;            // s4
  *--sp = 0;            // s3
  *--sp = 0;            // s2
  *--sp = 0;            // s1
  *--sp = 0;            // s0
  *--sp = (uint32_t)pc; // ra

  free_proc->ctx.stack_ptr = (uintptr_t)sp;

  TRACE("proc %s stack ptr: %x", free_proc->name, free_proc->ctx.stack_ptr);

  return free_proc;
}

Process *process_create(const char *name, uintptr_t pc) {
  Process *p = process_create_impl(procs, PROCS_MAX, name, pc);

  if (!p) {
    PANIC("reached proc limit");
  }

  return p;
}