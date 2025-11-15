#ifndef OT_KERNEL_HPP
#define OT_KERNEL_HPP

#include "ot/common.h"
#include "ot/lib/address.hpp"
#include "ot/lib/arguments.hpp"
#include "ot/lib/pair.hpp"
#include "ot/lib/string-view.hpp"

#ifdef OT_POSIX
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

// Placement new operator declaration (implemented in std.cpp)
void *operator new(size_t, void *ptr) noexcept;
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

#define TRACE_IPC(level, fmt, ...)                                             \
  do {                                                                         \
    if (LOG_IPC >= (level)) {                                                  \
      oprintf("[ipc] %s:%d: " fmt "\n", __FILE__, __LINE__, ##__VA_ARGS__);    \
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
/** Look up PageInfo given an address. */
PageInfo *page_info_lookup(PageAddr);
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
  int pid;
  ProcessState state;

  // TODO: Not necessary on WASM
  uintptr_t *page_table;

  /**
   * Communicates startup arguments in the form of a msgpack message,
   * if given. May be null.
   */
  PageAddr arg_page;

  /**
   * For syscalls that need more than 3 registers of storage
   * to communicate meaning to the kernel.
   *
   * Should always be a valid msgpack message.
   */
  PageAddr comm_page;

  /**
   * User-mode stack (separate from kernel stack)
   */
  PageAddr user_stack;

  /** PID that sent a message, if any */
  int msg_send_pid[OT_MSG_LIMIT];

  /** Pages for incoming messages */
  PageAddr msg_pages[OT_MSG_LIMIT];

  /** How many messages are waiting for the process to handle. Note that idx
   *  of the message in array is msg_count - 1
   */
  uint8_t msg_count;

  uintptr_t stack_ptr;
  uintptr_t user_pc;         // Save user program counter
  uintptr_t heap_next_vaddr; // Next available heap address
  bool kernel_mode;          // true = runs in kernel/supervisor mode, false = user mode
#ifdef OT_ARCH_WASM
  bool started; // For WASM: track if process has been started
  void *fiber;  // emscripten_fiber_t for this process
#endif
  uint8_t stack[8192] __attribute__((aligned(16)));
};

// Process management subsystem
Process *process_create_impl(Process *table, proc_id_t max_procs,
                             const char *name, const void *entry_point,
                             Arguments *args, bool kernel_mode = false);
Process *process_create(const char *name, const void *entry_point, Arguments *args, bool kernel_mode = false);
Process *process_next_runnable(void);
/** Looks up a process by name. Returns highest PID process that matches
 * (conflicts are allowed). */
Process *process_lookup(const StringView &name);
/** Looks up a process by PID, returns nullptr if process not runnable */
Process *process_lookup(int pid);
void process_exit(Process *proc);

// Gets the argument page pointer of the current process if possible
PageAddr process_get_arg_page();
// Gets the comm page pointer of the current process if possible
PageAddr process_get_comm_page();
PageAddr process_get_msg_page(int msg_idx);

// Allocates a page for the given process (physical addressing only)
// Returns PageAddr of allocated page, or null on failure
PageAddr process_alloc_mapped_page(Process *proc, bool readable,
                                   bool writable,
                                   bool executable);

extern Process *idle_proc, *current_proc;
extern Process procs[PROCS_MAX];

extern "C" void switch_context(uintptr_t *prev_sp, uintptr_t *next_sp);
void yield(void);

// Process for entering into a loaded userspace program (on RISCV)
extern "C" void user_entry(void);
extern "C" void kernel_start(void);

#ifdef OT_ARCH_WASM
void scheduler_loop(void);
#endif

#define USER_BASE 0x1000000
// Physical memory only - no virtual addressing
// USER_CODE_BASE and HEAP_BASE removed (not needed without MMU)
#define SSTATUS_SPIE (1 << 5)

// map_page() not used in physical-only mode
void map_page(uintptr_t *table1, uintptr_t vaddr, PageAddr paddr, uint32_t flags, proc_id_t pid);

// inter-process communication
bool ipc_send_message(Process *sender, int target_pid);
int ipc_pop_message(Process *receiver);

#endif