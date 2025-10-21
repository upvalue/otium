#include "ot/kernel/kernel.hpp"

extern "C" char __bss[], __bss_end[], __stack_top[];

// Forward declarations for test programs (defined in kernel-prog.cpp)
extern "C" void proc_hello_world(void);
extern "C" void proc_mem_test(void);
extern "C" void proc_alternate_a(void);
extern "C" void proc_alternate_b(void);
extern const char mem_test_image[];
void get_process_pages(uint32_t pid, uintptr_t *pages, uint32_t *count);

// Common kernel initialization - sets up idle process
void kernel_common(void) {
#ifndef OT_ARCH_WASM
  omemset(__bss, 0, (size_t)__bss_end - (size_t)__bss);
#endif
  TRACE(LSOFT, "hello from kernel_common");

  idle_proc = process_create("idle", nullptr, 0, false, nullptr);
  current_proc = idle_proc;
  TRACE(LSOFT, "created idle proc with name %s and pid %d", idle_proc->name,
        idle_proc->pid);
}
