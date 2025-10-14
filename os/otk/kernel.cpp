#include "otk/kernel.hpp"

extern "C" char __bss[], __bss_end[], __stack_top[];

void delay(void) {
  for (int i = 0; i < 30000000; i++)
    __asm__ __volatile__("nop"); // do nothing
}

Process *proc_a;
Process *proc_b;

extern "C" void proc_a_entry(void) {
  oprintf("starting process A\n");
  while (1) {
    oputchar('A');
    yield();
    delay();
  }
}

extern "C" void proc_b_entry(void) {
  oprintf("starting process B\n");
  while (1) {
    oputchar('B');
    yield();
    delay();
  }
}

void kernel_common(void) {
  omemset(__bss, 0, (size_t)__bss_end - (size_t)__bss);
  TRACE("hello from kernel_common");

  idle_proc = process_create("idle", (uintptr_t)nullptr);
  current_proc = idle_proc;

  TRACE("created idle proc with name %s and pid %d", idle_proc->name,
        idle_proc->pid);

  proc_a = process_create("proc_a", (uintptr_t)proc_a_entry);
  TRACE("created proc with name %s and pid %d", proc_a->name, proc_a->pid);
  proc_b = process_create("proc_b", (uintptr_t)proc_b_entry);
  TRACE("created proc with name %s and pid %d", proc_b->name, proc_b->pid);

  proc_a_entry();

  wfi();
}
