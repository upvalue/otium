#include "ot/core/kernel.hpp"
#include "ot/lib/mpack/mpack-writer.hpp"
#include "ot/lib/page-allocator.hpp"

extern char __kernel_base[];

Process procs[PROCS_MAX];

Process *current_proc = nullptr, *idle_proc = nullptr;

// Binary loading removed - all code now linked together in single executable

void map_page(uintptr_t *table1, uintptr_t vaddr, PageAddr paddr, uint32_t flags, proc_id_t pid) {
  if (!is_aligned((void *)vaddr, OT_PAGE_SIZE))
    PANIC("unaligned vaddr %x", vaddr);

  if (!paddr.aligned(OT_PAGE_SIZE))
    PANIC("unaligned paddr %x", paddr.raw());

  uint32_t vpn1 = (vaddr >> 22) & 0x3ff;
  if ((table1[vpn1] & PAGE_V) == 0) {
    // Create the 1st level page table if it doesn't exist.
    PageAddr pt_paddr = page_allocate(pid, 1);
    table1[vpn1] = ((pt_paddr.raw() / OT_PAGE_SIZE) << 10) | PAGE_V;
  }

  // Set the 2nd level page table entry to map the physical page.
  uint32_t vpn0 = (vaddr >> 12) & 0x3ff;
  uint32_t *table0 = (uint32_t *)((table1[vpn1] >> 10) * OT_PAGE_SIZE);
  table0[vpn0] = ((paddr.raw() / OT_PAGE_SIZE) << 10) | flags | PAGE_V;
}

Process *process_create_impl(Process *table, proc_id_t max_procs, const char *name, const void *entry_point,
                             Arguments *args, bool kernel_mode) {
  // Initialize memory tracking on first process creation
  memory_init();

  Process *free_proc = nullptr;
  proc_id_t i;
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

  free_proc->state = RUNNABLE;
  free_proc->pid = i;
  free_proc->kernel_mode = kernel_mode;

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
  omemset(storage_page.as_ptr(), 0, OT_PAGE_SIZE);
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
    Process *p = &procs[(current_proc->pid + i + 1) % PROCS_MAX];
    if (p->state == RUNNABLE && p->pid > 0) {
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

void process_exit(Process *proc) {
  TRACE_PROC(LSOFT, "Process %d (%s) exiting", proc->pid, proc->name);

  // Free all pages allocated to this process
  page_free_process(proc->pid);

  omemset(proc, 0, sizeof(Process));
  proc->state = UNUSED;
}

PageAddr process_get_arg_page() {
  PageAddr paddr = PageAddr(nullptr);
  if (current_proc == nullptr) {
    return paddr;
  }
  oprintf("process_get_arg_page: pid %d arg_page %p\n", current_proc->pid, current_proc->arg_page.as_ptr());
  return current_proc->arg_page;
}

PageAddr process_get_comm_page(void) {
  if (current_proc == nullptr) {
    return PageAddr(nullptr);
  }
  return current_proc->comm_page;
}

PageAddr process_alloc_mapped_page(Process *proc, bool readable, bool writable, bool executable) {
  if (!proc) {
    return PageAddr(nullptr);
  }

  // Allocate physical page
  PageAddr paddr = page_allocate(proc->pid, 1);
  if (paddr.is_null()) {
    return PageAddr(nullptr);
  }

  // Physical memory only - no virtual mapping needed
  // Permissions (readable, writable, executable) are not enforced in physical-only mode
  // User mode still provides fault isolation through privilege level
  return paddr;
}

Process *process_lookup(const StringView &name) {
  size_t i = PROCS_MAX - 1;
  while (true) {
    Process *p = &procs[i];
    if (p->state == RUNNABLE && strncmp(p->name, name.ptr, name.len) == 0) {
      return p;
    }
    if (i-- == 0) {
      break;
    }
  }
  return nullptr;
}

Process *process_lookup(int pid) {
  if (pid < 0 || pid >= PROCS_MAX) {
    return nullptr;
  }
  Process *p = &procs[pid];
  if (p->state != RUNNABLE) {
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