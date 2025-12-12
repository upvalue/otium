#include "ot/core/kernel.hpp"
#include "ot/lib/mpack/mpack-writer.hpp"
#include "ot/lib/page-allocator.hpp"
#include "ot/user/local-storage.hpp"

#ifdef OT_ARCH_WASM
#include <emscripten/fiber.h>
#endif

extern char __kernel_base[];

Process procs[PROCS_MAX];

Process *current_proc = nullptr, *idle_proc = nullptr;

// Globally unique process ID counter (never reused)
Pid proc_pid_counter = Pid(1);

// Lookup table: indexed by pidx, contains pid (PID_NONE if unused)
// Zero-initialized; PID_NONE == Pid(0) so this is correct
Pid process_pids[PROCS_MAX] = {};

// Binary loading removed - all code now linked together in single executable

void map_page(uintptr_t *table1, uintptr_t vaddr, PageAddr paddr, uint32_t flags, Pidx pidx) {
  if (!is_aligned((void *)vaddr, OT_PAGE_SIZE))
    PANIC("unaligned vaddr %x", vaddr);

  if (!paddr.aligned(OT_PAGE_SIZE))
    PANIC("unaligned paddr %x", paddr.raw());

  uint32_t vpn1 = (vaddr >> 22) & 0x3ff;
  if ((table1[vpn1] & PAGE_V) == 0) {
    // Create the 1st level page table if it doesn't exist.
    PageAddr pt_paddr = page_allocate(pidx, 1);
    table1[vpn1] = ((pt_paddr.raw() / OT_PAGE_SIZE) << 10) | PAGE_V;
  }

  // Set the 2nd level page table entry to map the physical page.
  uint32_t vpn0 = (vaddr >> 12) & 0x3ff;
  uint32_t *table0 = (uint32_t *)((table1[vpn1] >> 10) * OT_PAGE_SIZE);
  table0[vpn0] = ((paddr.raw() / OT_PAGE_SIZE) << 10) | flags | PAGE_V;
}

Process *process_create_impl(Process *table, int max_procs, const char *name, const void *entry_point, Arguments *args,
                             bool kernel_mode) {
  // Initialize memory tracking on first process creation
  memory_init();

  Process *free_proc = nullptr;
  int i;
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

  memset(free_proc, 0, sizeof(Process));

  for (int j = 0; j < 32; j++) {
    if (!name[j]) {
      break;
    }
    free_proc->name[j] = name[j];
  }

  free_proc->state = RUNNABLE;
  free_proc->pidx = Pidx(i);
  free_proc->pid = proc_pid_counter++;
  free_proc->kernel_mode = kernel_mode;

  // Update lookup table
  process_pids[i] = free_proc->pid;

  // Set user_pc to physical address - no virtual memory needed
  if (entry_point) {
    free_proc->user_pc = (uintptr_t)entry_point;
  } else {
    free_proc->user_pc = 0;
  }

  free_proc->heap_next_vaddr = 0; // Not used in physical-only mode

  // Set up initial stack with zeroed out registers
  uintptr_t *sp = (uintptr_t *)&free_proc->stack[sizeof(free_proc->stack)];
  *--sp = 0; // s11
  *--sp = 0; // s10
  *--sp = 0; // s9
  *--sp = 0; // s8
  *--sp = 0; // s7
  *--sp = 0; // s6
  *--sp = 0; // s5
  *--sp = 0; // s4
  *--sp = 0; // s3
  *--sp = 0; // s2
  *--sp = 0; // s1
  *--sp = 0; // s0

  // All programs now use function pointers (no binary loading)
  // For user-mode processes: use user_entry as ra to drop to user mode
  // For kernel-mode processes: jump directly to entry point
  if (kernel_mode) {
    *--sp = (uintptr_t)entry_point; // ra - kernel mode, call entry directly
  } else {
    *--sp = (uintptr_t)user_entry; // ra - drops to user mode and jumps to user_pc
  }

  free_proc->stack_ptr = (uintptr_t)sp;

  // No page tables - using physical memory only for simplicity and RP2350 compatibility
  // User mode still provides fault isolation even without virtual memory
  free_proc->page_table = nullptr;

  PageAddr comm_page = process_alloc_mapped_page(free_proc, true, true, false);
  if (comm_page.is_null()) {
    PANIC("failed to allocate comm page");
  }

  MPackWriter msg(comm_page.as<char>(), OT_PAGE_SIZE);
  msg.nil();

  free_proc->comm_page = comm_page;

  // Allocate local storage page for process-specific data
  PageAddr storage_page = process_alloc_mapped_page(free_proc, true, true, false);
  if (storage_page.is_null()) {
    PANIC("failed to allocate storage page");
  }
  // Zero out the storage page
  memset(storage_page.as_ptr(), 0, OT_PAGE_SIZE);
  free_proc->storage_page = storage_page;

  // Allocate user-mode stack (separate from kernel stack)
  PageAddr user_stack = process_alloc_mapped_page(free_proc, true, true, false);
  if (user_stack.is_null()) {
    PANIC("failed to allocate user stack");
  }
  free_proc->user_stack = user_stack;

  // Handle argument array
  if (args) {
    PageAddr arg_page = process_alloc_mapped_page(free_proc, true, false, false);
    if (arg_page.is_null()) {
      PANIC("failed to allocate arg page");
    }

    MPackWriter msg(arg_page.as<char>(), OT_PAGE_SIZE);
    msg.map(1).str("args").stringarray(args->argc, args->argv);

    free_proc->arg_page = arg_page;
  }

  memory_increment_process_count();

  return free_proc;
}

Process *process_create(const char *name, const void *entry_point, Arguments *args, bool kernel_mode) {
  Process *p = process_create_impl(procs, PROCS_MAX, name, entry_point, args, kernel_mode);

  if (!p) {
    PANIC("reached proc limit");
  }

  return p;
}

Process *process_next_runnable(void) {
  Process *next = idle_proc;
  for (size_t i = 0; i < PROCS_MAX; i++) {
    Process *p = &procs[(current_proc->pidx.raw() + i + 1) % PROCS_MAX];
    // Skip processes in IPC_RECV_WAIT or IPC_SEND_WAIT state - they'll be woken when message arrives or reply returns
    if (p->state == RUNNABLE && p->pidx > Pidx(0)) {
      next = p;
      break;
    }
  }

  // for now we just quit when shell exits as a convenience
  if (procs[1].state == TERMINATED) {
    oprintf("process 1 terminated; exiting\n");
    return idle_proc;
  }

  return next;
}

void process_switch_to(Process *target) {
  Process *prev = current_proc;
  TRACE_IPC(LLOUD, "IPC switch from pidx %d to %d (pid %lu to %lu)", prev->pidx, target->pidx, prev->pid, target->pid);

#ifdef OT_ARCH_RISCV
  current_proc = target;

  // Update local_storage to point to target process's storage
  // This is critical for memory allocation (TLSF) to use the correct pool
  extern LocalStorage *local_storage;
  local_storage = (LocalStorage *)target->storage_page.as_ptr();

  // Set supervisor scratch register to target process's kernel stack
  // Set sepc to target process's user PC
  __asm__ __volatile__("csrw sscratch, %[sscratch]\n"
                       "csrw sepc, %[sepc]\n"
                       :
                       : [sscratch] "r"((uintptr_t)&target->stack[sizeof(target->stack)]), [sepc] "r"(target->user_pc)
                       :);
  switch_context(&prev->stack_ptr, &target->stack_ptr);
#endif

#ifdef OT_ARCH_WASM
  // For WASM, we can't directly swap between process fibers
  // We must go through the scheduler fiber
  // IMPORTANT: Don't set current_proc yet! yield() needs it to point to prev
  extern void wasm_switch_to_process(Process * prev, Process * target);
  wasm_switch_to_process(prev, target);
  // When we return, scheduler has restored current_proc and local_storage correctly
#endif
}

void process_exit(Process *proc, bool zero_proc) {
  TRACE_PROC(LSOFT, "Process pidx=%d pid=%lu (%s) exiting", proc->pidx.raw(), proc->pid.raw(), proc->name);

  // Release any known memory regions held by this process
  uint32_t known_released = known_memory_release_process(proc->pidx);

  // Free all pages allocated to this process
  uint32_t pages_freed = page_free_process(proc->pidx);

  TRACE_MEM(LSOFT, "Process %s (pidx=%d) freed %d pages, released %d known memory regions", proc->name,
            proc->pidx.raw(), pages_freed, known_released);

  // Clear lookup table entry
  process_pids[proc->pidx.raw()] = PID_NONE;

  if (zero_proc) {
    memset(proc, 0, sizeof(Process));
  }
  proc->state = UNUSED;
}

void shutdown_all_processes(void) {
  oprintf("Shutting down all processes...\n");
  for (int i = 0; i < PROCS_MAX; i++) {
    Process *proc = &procs[i];
    if (proc->state != UNUSED) {
      oprintf("Terminating process %s (pidx=%d, pid=%lu)\n", proc->name, proc->pidx.raw(), proc->pid.raw());
      process_exit(proc, false);
    }
  }
  oprintf("All processes terminated, exiting kernel\n");
  kernel_exit();
}

PageAddr process_get_arg_page() {
  PageAddr paddr = PageAddr(nullptr);
  if (current_proc == nullptr) {
    return paddr;
  }
  /*oprintf("process_get_arg_page: pidx %d pid %lu arg_page %p\n", current_proc->pidx.raw(), current_proc->pid.raw(),
          current_proc->arg_page.as_ptr());*/
  return current_proc->arg_page;
}

PageAddr process_get_comm_page(void) {
  if (current_proc == nullptr) {
    return PageAddr(nullptr);
  }
  return current_proc->comm_page;
}

PageAddr process_alloc_mapped_page(Process *proc, bool readable, bool writable, bool executable) {
  return process_alloc_mapped_pages(proc, 1, readable, writable, executable);
}

PageAddr process_alloc_mapped_pages(Process *proc, size_t page_count, bool readable, bool writable, bool executable) {
  if (!proc) {
    return PageAddr(nullptr);
  }

  // Allocate contiguous physical pages
  PageAddr paddr = page_allocate(proc->pidx, page_count);
  if (paddr.is_null()) {
    return PageAddr(nullptr);
  }

  // Physical memory only - no virtual mapping needed
  // Permissions (readable, writable, executable) are not enforced in physical-only mode
  // User mode still provides fault isolation through privilege level
  return paddr;
}

// Helper to find pidx from pid (returns -1 if not found)
Pidx process_lookup_by_pid(Pid pid) {
  for (int i = 0; i < PROCS_MAX; i++) {
    if (process_pids[i] == pid && procs[i].state != UNUSED) {
      return Pidx(i);
    }
  }
  return PIDX_INVALID; // Not found
}

// Lookup process by name, returns pid (user-facing identifier)
Pid process_lookup(const StringView &name) {
  size_t i = PROCS_MAX - 1;
  while (true) {
    Process *p = &procs[i];
    // Check if process is running (RUNNABLE, IPC_RECV_WAIT, or IPC_SEND_WAIT - services waiting for messages should be findable)
    if (process_is_running(p) && strncmp(p->name, name.ptr, name.len) == 0 &&
        p->name[name.len] == '\0') { // Ensure exact match
      return p->pid;                 // Return pid (not pidx)
    }
    if (i-- == 0) {
      break;
    }
  }
  return PID_NONE; // Not found
}

// Internal: lookup process by pidx
Process *process_lookup_by_pidx(Pidx pidx) {
  int idx = pidx.raw();
  if (idx < 0 || idx >= PROCS_MAX) {
    return nullptr;
  }
  Process *p = &procs[idx];
  // Allow lookup of running processes (RUNNABLE, IPC_RECV_WAIT, or IPC_SEND_WAIT - e.g., services waiting for messages)
  if (!process_is_running(p)) {
    return nullptr;
  }
  return p;
}

PageAddr process_get_storage_page(void) {
  if (current_proc == nullptr) {
    return PageAddr(nullptr);
  }
  return current_proc->storage_page;
}

// Forward declaration for user_program_main
extern void user_program_main(void);

// Program registry - maps name to entry point
// All user programs go through user_program_main which reads the name from args
struct ProgramEntry {
  const char *name;
};

static const ProgramEntry program_registry[] = {
    {"shell"}, {"uishell"},    {"scratch"}, {"spacedemo"}, {"typedemo"},
    {"echo"},  {"gfxscratch"}, {"edit"},    {nullptr} // Sentinel
};

// Helper to check if program name is in registry
static bool is_valid_program(const char *name) {
  for (int i = 0; program_registry[i].name != nullptr; i++) {
    if (strcmp(program_registry[i].name, name) == 0) {
      return true;
    }
  }
  return false;
}

Pid kernel_spawn_process(const char *name, int argc, char **argv) {
  // Validate program name
  if (!is_valid_program(name)) {
    TRACE_PROC(LSOFT, "spawn failed: unknown program '%s'", name);
    return PID_NONE;
  }

  // Build arguments - use the provided argv
  Arguments args = {(uintptr_t)argc, argv};

  // All user programs use user_program_main as entry point
  // The program name in argv[0] determines which *_main() gets called
  Process *proc = process_create(name, (const void *)user_program_main, &args, false);
  if (proc == nullptr) {
    TRACE_PROC(LSOFT, "spawn failed: could not create process for '%s'", name);
    return PID_NONE;
  }

  TRACE_PROC(LSOFT, "spawned process '%s' with pid %lu", name, proc->pid.raw());
  return proc->pid;
}