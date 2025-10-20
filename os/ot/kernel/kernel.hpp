#ifndef OT_KERNEL_HPP
#define OT_KERNEL_HPP

#include "ot/common.h"
#include "ot/shared/address.hpp"

#ifdef OT_TEST
#include <stdlib.h>
#define PANIC(fmt, ...)                                                        \
  do {                                                                         \
    oprintf("PANIC: %s:%d: " fmt "\n", __FILE__, __LINE__, ##__VA_ARGS__);     \
    exit(1);                                                                   \
  } while (0)
#else
#define PANIC(fmt, ...)                                                        \
  do {                                                                         \
    oprintf("PANIC: %s:%d: " fmt "\n", __FILE__, __LINE__, ##__VA_ARGS__);     \
    while (1) {                                                                \
    }                                                                          \
  } while (0)
#endif

#define TRACE(level, fmt, ...)                                                 \
  do {                                                                         \
    if (LOG_GENERAL >= (level)) {                                              \
      oprintf("[dbg] %s:%d: " fmt "\n", __FILE__, __LINE__, ##__VA_ARGS__);    \
    }                                                                          \
  } while (0)

#define TRACE_MEM(level, fmt, ...)                                             \
  do {                                                                         \
    if (LOG_MEM >= (level)) {                                                  \
      oprintf("[mem] %s:%d: " fmt "\n", __FILE__, __LINE__, ##__VA_ARGS__);    \
    }                                                                          \
  } while (0)

#define TRACE_PROC(level, fmt, ...)                                            \
  do {                                                                         \
    if (LOG_PROC >= (level)) {                                                 \
      oprintf("[proc] %s:%d: " fmt "\n", __FILE__, __LINE__, ##__VA_ARGS__);   \
    }                                                                          \
  } while (0)

// platform specific utility functions
void wfi(void);
void kernel_exit(void);

void kernel_common(void);

// memory management
typedef int32_t proc_id_t;

struct PageInfo {
  int32_t pid;    // Process ID that owns this page (0 = free)
  PageAddr addr;  // Physical address of the page
  PageInfo *next; // For free list linking
};

struct MemoryStats {
  uint32_t total_pages;
  uint32_t allocated_pages;
  uint32_t freed_pages;
  uint32_t processes_created;
  uint32_t peak_usage_pages;
};

PageAddr page_allocate(proc_id_t pid, size_t page_count);
void page_free_process(proc_id_t pid);
void memory_init();
void memory_report();
void memory_increment_process_count();
#ifndef OT_ARCH_WASM
extern "C" char __free_ram[], __free_ram_end[];
#else
extern "C" char *__free_ram, *__free_ram_end;
#endif

// process management
#define PROCS_MAX 8

#define SATP_SV32 (1u << 31)
#define PAGE_V (1 << 0) // "Valid" bit (entry is enabled)
#define PAGE_R (1 << 1) // Readable
#define PAGE_W (1 << 2) // Writable
#define PAGE_X (1 << 3) // Executable
#define PAGE_U (1 << 4) // User (accessible in user mode)

enum ProcessState { UNUSED, RUNNABLE, TERMINATED };

struct Process {
  char name[32];
  uint32_t pid;
  ProcessState state;
  uintptr_t *page_table;
  uintptr_t stack_ptr;
  uintptr_t user_pc;         // Save user program counter
  uintptr_t heap_next_vaddr; // Next available heap address
  uint8_t stack[8192] __attribute__((aligned(16)));
#ifdef OT_ARCH_WASM
  bool started; // For WASM: track if process has been started
  void *fiber;  // emscripten_fiber_t for this process
  uint8_t asyncify_stack[8192]
      __attribute__((aligned(16))); // Asyncify stack for fiber
#endif
};

// Process management subsystem
Process *process_create_impl(Process *table, proc_id_t max_procs,
                             const char *name, const void *image_or_pc,
                             size_t size, bool is_image);
Process *process_create(const char *name, const void *image_or_pc, size_t size,
                        bool is_image);
Process *process_next_runnable(void);
void process_exit(Process *proc);

extern Process *idle_proc, *current_proc;
extern Process procs[PROCS_MAX];

extern "C" void switch_context(uintptr_t *prev_sp, uintptr_t *next_sp);
void yield(void);

#ifdef OT_ARCH_WASM
void scheduler_loop(void);
#endif

// Memory management subsystem
void map_page(uintptr_t *table1, uintptr_t vaddr, PageAddr paddr,
              uint32_t flags, proc_id_t pid);

/** Look up PageInfo given an address. */
PageInfo *page_info_lookup(PageAddr);

#define USER_BASE 0x1000000
#define HEAP_BASE 0x2000000
#define SSTATUS_SPIE (1 << 5)

// Process for entering into a loaded userspace program (on RISCV)
extern "C" void user_entry(void);

extern "C" void kernel_start(void);

#endif