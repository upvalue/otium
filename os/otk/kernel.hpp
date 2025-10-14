#ifndef OTK_KERNEL_HPP
#define OTK_KERNEL_HPP

#include "otcommon.h"

#define PAGE_SIZE 4096

#define PANIC(fmt, ...)                                                        \
  do {                                                                         \
    oprintf("PANIC: %s:%d: " fmt "\n", __FILE__, __LINE__, ##__VA_ARGS__);     \
    while (1) {                                                                \
    }                                                                          \
  } while (0)

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
void *page_allocate_impl(char *begin, char *end, char **next, size_t page_size);
void *page_allocate(size_t n);
extern "C" char __free_ram[], __free_ram_end[];

// process management
#define PROCS_MAX 8

#define SATP_SV32 (1u << 31)
#define PAGE_V (1 << 0) // "Valid" bit (entry is enabled)
#define PAGE_R (1 << 1) // Readable
#define PAGE_W (1 << 2) // Writable
#define PAGE_X (1 << 3) // Executable
#define PAGE_U (1 << 4) // User (accessible in user mode)

enum ProcessState { UNUSED, RUNNABLE };

struct ProcessContext {
  uintptr_t stack_ptr;
};

struct Process {
  char name[32];
  uint32_t pid;
  ProcessState state;
  ProcessContext ctx;
  uintptr_t *page_table;
  uint8_t stack[8192];
};

Process *process_create_impl(Process *table, uint32_t max_procs,
                             const char *name, uint32_t pc);
Process *process_create(const char *name, uintptr_t pc);
Process *process_next_runnable(void);
void process_exit(Process *proc);

extern Process *idle_proc, *current_proc;
extern Process procs[PROCS_MAX];

extern "C" void switch_context(uintptr_t *prev_sp, uintptr_t *next_sp);
void yield(void);

#endif