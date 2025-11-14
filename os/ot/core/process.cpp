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
                             Arguments *args) {
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

  // Set user_pc to the user-accessible mapping of the entry point
  // entry_point is a physical address in kernel space, we map it to USER_CODE_BASE for user mode
  if (entry_point) {
    free_proc->user_pc = USER_CODE_BASE + ((uintptr_t)entry_point - (uintptr_t)__kernel_base);
  } else {
    free_proc->user_pc = 0;
  }

  free_proc->heap_next_vaddr = HEAP_BASE;


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
  // Use user_entry as ra to drop to user mode before executing entry_point
  *--sp = (uintptr_t)user_entry; // ra - drops to user mode and jumps to user_pc

  free_proc->stack_ptr = (uintptr_t)sp;

#ifndef OT_ARCH_WASM
  // Dual mapping approach: map kernel code at two virtual addresses
  // 1. Identity mapped (0x80200000+) WITHOUT PAGE_U - for supervisor mode execution
  // 2. High address (USER_CODE_BASE) WITH PAGE_U - for user mode execution
  // This allows supervisor mode to safely switch page tables while user mode can execute code
  PageAddr page_table = page_allocate(i, 1);

  // Identity mapping WITH PAGE_U so user mode can access data/strings at compiled addresses
  // Supervisor needs SUM bit to access these pages (already enabled in kernel_main)
  // Problem: supervisor can't fetch instructions from PAGE_U pages (SUM only affects data)
  // Solution: We'll need to be careful about when we write satp
  for (uintptr_t paddr = (uintptr_t)__kernel_base; paddr < (uintptr_t)__free_ram_end; paddr += OT_PAGE_SIZE)
    map_page(page_table.as<uintptr_t>(), paddr, PageAddr(paddr), PAGE_R | PAGE_W | PAGE_X | PAGE_U, i);

  // User-mode mapping (mapped to USER_CODE_BASE with PAGE_U)
  // Need R+W+X so user code can execute, read data, and write to stack
  for (uintptr_t paddr = (uintptr_t)__kernel_base; paddr < (uintptr_t)__free_ram_end; paddr += OT_PAGE_SIZE) {
    uintptr_t vaddr = USER_CODE_BASE + (paddr - (uintptr_t)__kernel_base);
    map_page(page_table.as<uintptr_t>(), vaddr, PageAddr(paddr), PAGE_R | PAGE_W | PAGE_X | PAGE_U, i);
  }

  // Map VirtIO MMIO regions (identity mapped)
  for (int mmio_idx = 0; mmio_idx < 8; mmio_idx++) {
    uintptr_t mmio_addr = 0x10001000 + (mmio_idx * 0x1000);
    map_page(page_table.as<uintptr_t>(), mmio_addr, PageAddr(mmio_addr), PAGE_R | PAGE_W, i);
  }

  free_proc->page_table = page_table.as<uintptr_t>();
#else
  // WASM: No page tables needed, use direct memory access
  free_proc->page_table = nullptr;
#endif

  Pair<PageAddr, PageAddr> comm_page = process_alloc_mapped_page(free_proc, true, true, false);
  if (comm_page.first.is_null() || comm_page.second.is_null()) {
    PANIC("failed to allocate comm page");
  }

  MPackWriter msg(comm_page.first.as<char>(), OT_PAGE_SIZE);
  msg.nil();

  free_proc->comm_page = comm_page;

  // Allocate user-mode stack (separate from kernel stack)
  Pair<PageAddr, PageAddr> user_stack = process_alloc_mapped_page(free_proc, true, true, false);
  if (user_stack.first.is_null() || user_stack.second.is_null()) {
    PANIC("failed to allocate user stack");
  }
  free_proc->user_stack = user_stack;

  // Handle argument array
  if (args) {

    Pair<PageAddr, PageAddr> alloc_result = process_alloc_mapped_page(free_proc, true, false, false);
    char *buffer = alloc_result.first.as<char>();

    MPackWriter msg(buffer, OT_PAGE_SIZE);

    msg.map(1).str("args").stringarray(args->argc, args->argv);

    // Store virtual address for user access
    free_proc->arg_page = alloc_result.second;
  }

  memory_increment_process_count();

  return free_proc;
}

Process *process_create(const char *name, const void *entry_point, Arguments *args) {
  Process *p = process_create_impl(procs, PROCS_MAX, name, entry_point, args);

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
  return current_proc->arg_page;
}

Pair<PageAddr, PageAddr> empty_page_pair = make_pair(PageAddr(nullptr), PageAddr(nullptr));

Pair<PageAddr, PageAddr> process_get_comm_page(void) {
  if (current_proc == nullptr) {
    return empty_page_pair;
  }
  return current_proc->comm_page;
}

Pair<PageAddr, PageAddr> process_get_msg_page(int msg_idx) {
  if (current_proc == nullptr) {
    return empty_page_pair;
  }
  return current_proc->msg_pages[msg_idx];
}

Pair<PageAddr, PageAddr> process_alloc_mapped_page(Process *proc, bool readable, bool writable, bool executable) {
  if (!proc) {
    return empty_page_pair;
  }

  // Allocate physical page
  PageAddr paddr = page_allocate(proc->pid, 1);
  if (paddr.is_null()) {
    return empty_page_pair;
  }

#ifndef OT_ARCH_WASM
  // RISC-V: Use MMU to map page to virtual address with specified permissions
  // Pages are user-accessible since processes run in user mode
  uint32_t flags = PAGE_U;
  if (readable)
    flags |= PAGE_R;
  if (writable)
    flags |= PAGE_W;
  if (executable)
    flags |= PAGE_X;

  uintptr_t vaddr_raw = proc->heap_next_vaddr;
  map_page(proc->page_table, vaddr_raw, paddr, flags, proc->pid);
  proc->heap_next_vaddr += OT_PAGE_SIZE;
  return make_pair(paddr, PageAddr(vaddr_raw));
#else
  // WASM: No MMU, physical address = virtual address
  // Permissions are not enforced in WASM
  proc->heap_next_vaddr += OT_PAGE_SIZE;
  return make_pair(paddr, paddr);
#endif
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