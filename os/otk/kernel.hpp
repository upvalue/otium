#ifndef OTK_KERNEL_HPP
#define OTK_KERNEL_HPP

#include "otcommon.h"

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

#define TRACE(fmt, ...)                                                        \
  do {                                                                         \
    oprintf("TRACE: %s:%d: " fmt "\n", __FILE__, __LINE__, ##__VA_ARGS__);     \
  } while (0)

#ifdef OT_TRACE_MEM
#define TRACE_MEM(fmt, ...)                                                    \
  do {                                                                         \
    oprintf("TRACE_MEM: %s:%d: " fmt "\n", __FILE__, __LINE__, ##__VA_ARGS__); \
  } while (0)
#else
#define TRACE_MEM(fmt, ...)
#endif

// platform specific utility functions
void wfi(void);
void kernel_exit(void);

void kernel_common(void);

// memory management
struct PageInfo {
  uint32_t pid;   // Process ID that owns this page (0 = free)
  uintptr_t addr; // Physical address of the page
  PageInfo *next; // For free list linking
};

struct MemoryStats {
  uint32_t total_pages;
  uint32_t allocated_pages;
  uint32_t freed_pages;
  uint32_t processes_created;
  uint32_t peak_usage_pages;
};

void *page_allocate(uint32_t pid, size_t page_count);
void page_free_process(uint32_t pid);
void memory_init();
void memory_report();
void memory_increment_process_count();
#ifndef __EMSCRIPTEN__
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
  uint8_t stack[8192];
};

Process *process_create_impl(Process *table, uint32_t max_procs,
                             const char *name, const void *image_or_pc,
                             size_t size, bool is_image);
Process *process_create(const char *name, const void *image_or_pc, size_t size,
                        bool is_image);
Process *process_next_runnable(void);
void process_exit(Process *proc);
void map_page(uintptr_t *table1, uintptr_t vaddr, uintptr_t paddr,
              uint32_t flags, uint32_t pid);

extern Process *idle_proc, *current_proc;
extern Process procs[PROCS_MAX];

extern "C" void switch_context(uintptr_t *prev_sp, uintptr_t *next_sp);
void yield(void);

#define USER_BASE 0x1000000
#define HEAP_BASE 0x2000000
#define SSTATUS_SPIE (1 << 5)

extern "C" void user_entry(void);

#endif