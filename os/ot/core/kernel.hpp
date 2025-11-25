#ifndef OT_KERNEL_HPP
#define OT_KERNEL_HPP

#include "ot/common.h"
#include "ot/lib/address.hpp"
#include "ot/lib/arguments.hpp"
#include "ot/lib/ipc.hpp"
#include "ot/lib/pair.hpp"
#include "ot/lib/string-view.hpp"
#include "ot/lib/typed-int.hpp"

#ifdef OT_POSIX
#include <stdlib.h>
#define PANIC(fmt, ...)                                                                                                \
  do {                                                                                                                 \
    oprintf("PANIC: %s:%d: " fmt "\n", __FILE__, __LINE__, ##__VA_ARGS__);                                             \
    exit(1);                                                                                                           \
  } while (0)
#else
#define PANIC(fmt, ...)                                                                                                \
  do {                                                                                                                 \
    oprintf("PANIC: %s:%d: " fmt "\n", __FILE__, __LINE__, ##__VA_ARGS__);                                             \
    while (1) {                                                                                                        \
    }                                                                                                                  \
  } while (0)
#endif

#define TRACE(level, fmt, ...)                                                                                         \
  do {                                                                                                                 \
    if (LOG_GENERAL >= (level)) {                                                                                      \
      oprintf("[dbg] %s:%d: " fmt "\n", __FILE__, __LINE__, ##__VA_ARGS__);                                            \
    }                                                                                                                  \
  } while (0)

#define TRACE_MEM(level, fmt, ...)                                                                                     \
  do {                                                                                                                 \
    if (LOG_MEM >= (level)) {                                                                                          \
      oprintf("[mem] %s:%d: " fmt "\n", __FILE__, __LINE__, ##__VA_ARGS__);                                            \
    }                                                                                                                  \
  } while (0)

#define TRACE_PROC(level, fmt, ...)                                                                                    \
  do {                                                                                                                 \
    if (LOG_PROC >= (level)) {                                                                                         \
      oprintf("[proc] %s:%d: " fmt "\n", __FILE__, __LINE__, ##__VA_ARGS__);                                           \
    }                                                                                                                  \
  } while (0)

#define TRACE_IPC(level, fmt, ...)                                                                                     \
  do {                                                                                                                 \
    if (LOG_IPC >= (level)) {                                                                                          \
      oprintf("[ipc] %s:%d: " fmt "\n", __FILE__, __LINE__, ##__VA_ARGS__);                                            \
    }                                                                                                                  \
  } while (0)

// platform specific utility functions
void wfi(void);
void kernel_exit(void);

void kernel_common(void);

// memory management
struct PageInfo {
  Pidx pidx;      // Process index that owns this page (PIDX_NONE = free)
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

PageAddr page_allocate(Pidx pidx, size_t page_count);
/** Look up PageInfo given an address. */
PageInfo *page_info_lookup(PageAddr);
void page_free_process(Pidx pidx);
void memory_init();
void memory_report();
void memory_increment_process_count();
#ifndef OT_ARCH_WASM
extern "C" char __free_ram[], __free_ram_end[];
#else
extern "C" char *__free_ram, *__free_ram_end;
#endif

// Known memory reservation system
struct KnownMemoryInfo {
  PageAddr addr;     // Base address of reserved memory
  size_t page_count; // Number of pages reserved
  Pidx holder_pidx;  // Process holding the lock (PIDX_NONE = free)
};

extern KnownMemoryInfo known_memory_table[KNOWN_MEMORY_COUNT];

void known_memory_init();
PageAddr known_memory_lock(KnownMemory km, size_t page_count, Pidx pidx);
void known_memory_release_process(Pidx pidx);

// process management
#define PROCS_MAX 16

#define SATP_SV32 (1u << 31)
#define PAGE_V (1 << 0) // "Valid" bit (entry is enabled)
#define PAGE_R (1 << 1) // Readable
#define PAGE_W (1 << 2) // Writable
#define PAGE_X (1 << 3) // Executable
#define PAGE_U (1 << 4) // User (accessible in user mode)

enum ProcessState { UNUSED, RUNNABLE, TERMINATED, IPC_WAIT };

// Globally unique process ID counter (never reused)
extern Pid proc_pid_counter;

// Lookup table: indexed by pidx, contains pid (PID_NONE if unused)
extern Pid process_pids[PROCS_MAX];

struct Process {
  char name[32];
  Pidx pidx; // Process index (0-7, reused) - kernel internal only
  Pid pid;   // Process ID (globally unique, never reused) - user-facing
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

  /**
   * Per-process local storage page for user-space data.
   * Updated by kernel on context switch.
   */
  PageAddr storage_page;

  uintptr_t stack_ptr;
  uintptr_t user_pc;         // Save user program counter
  uintptr_t heap_next_vaddr; // Next available heap address
  bool kernel_mode;          // true = runs in kernel/supervisor mode, false = user mode

  // IPC fields
  IpcMessage pending_message;   // Message waiting to be received
  bool has_pending_message;     // Flag for message availability
  Process *blocked_sender;      // Pointer to sender waiting for reply
  IpcResponse pending_response; // Response storage for blocked sender

#ifdef OT_ARCH_WASM
  bool started; // For WASM: track if process has been started
  void *fiber;  // emscripten_fiber_t for this process
#endif
  uint8_t stack[8192] __attribute__((aligned(16)));
};

// Helper to check if a process is in a running state (RUNNABLE or IPC_WAIT)
inline bool process_is_running(const Process *p) { return p->state == RUNNABLE || p->state == IPC_WAIT; }

// Helper to find pidx from pid (returns PIDX_INVALID if not found)
Pidx process_lookup_by_pid(Pid pid);

// Process management subsystem
Process *process_create_impl(Process *table, int max_procs, const char *name, const void *entry_point, Arguments *args,
                             bool kernel_mode = false);
Process *process_create(const char *name, const void *entry_point, Arguments *args, bool kernel_mode = false);
Process *process_next_runnable(void);
/** Looks up a process by name. Returns pid (globally unique, user-facing).
 * Returns highest pidx process that matches (conflicts are allowed). */
Pid process_lookup(const StringView &name);
/** Internal: Looks up a process by pidx, returns nullptr if process not runnable */
Process *process_lookup_by_pidx(Pidx pidx);
void process_exit(Process *proc);
void shutdown_all_processes(void);

// Gets the argument page pointer of the current process if possible
PageAddr process_get_arg_page();
// Gets the comm page pointer of the current process if possible
PageAddr process_get_comm_page();
// Gets the storage page pointer of the current process if possible
PageAddr process_get_storage_page();

// Allocates a page for the given process (physical addressing only)
// Returns PageAddr of allocated page, or null on failure
PageAddr process_alloc_mapped_page(Process *proc, bool readable, bool writable, bool executable);

// map_page() not used in physical-only mode
void map_page(uintptr_t *table1, uintptr_t vaddr, PageAddr paddr, uint32_t flags, Pidx pidx);

extern Process *idle_proc, *current_proc;
extern Process procs[PROCS_MAX];

extern "C" void switch_context(uintptr_t *prev_sp, uintptr_t *next_sp);
void yield(void);
void process_switch_to(Process *target);

// Process for entering into a loaded userspace program (on RISCV)
extern "C" void user_entry(void);
void kernel_start(void);

#ifdef OT_ARCH_WASM
void scheduler_loop(void);
#endif

#define USER_BASE 0x1000000
// Physical memory only - no virtual addressing
// USER_CODE_BASE and HEAP_BASE removed (not needed without MMU)
#define SSTATUS_SPIE (1 << 5)

#endif