#include "ot/kernel/kernel.hpp"

extern char __kernel_base[];

Process procs[PROCS_MAX];

Process *current_proc = nullptr, *idle_proc = nullptr;

void map_page(uintptr_t *table1, uintptr_t vaddr, PageAddr paddr,
              uint32_t flags, proc_id_t pid) {
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

Process *process_create_impl(Process *table, proc_id_t max_procs,
                             const char *name, const void *image_or_pc,
                             size_t size, bool is_image) {
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
  free_proc->user_pc = is_image ? USER_BASE : (uintptr_t)image_or_pc;
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

  if (is_image) {
    *--sp = (uintptr_t)user_entry; // ra
  } else {
    *--sp = (uintptr_t)image_or_pc; // ra
  }

  free_proc->stack_ptr = (uintptr_t)sp;

#ifndef OT_ARCH_WASM
  // Map kernel pages.
  PageAddr page_table = page_allocate(i, 1);
  for (uintptr_t paddr = (uintptr_t)__kernel_base;
       paddr < (uintptr_t)__free_ram_end; paddr += OT_PAGE_SIZE)
    map_page(page_table.as<uintptr_t>(), paddr, PageAddr(paddr), PAGE_R | PAGE_W | PAGE_X,
             i);

  // Map user pages.
  if (is_image) {
    TRACE_PROC(LLOUD, "found image. allocating pages");
    for (size_t off = 0; off < size; off += OT_PAGE_SIZE) {
      PageAddr page = page_allocate(i, 1);

      // Handle the case where the data to be copied is smaller than the
      // page size.
      size_t remaining = size - off;
      size_t copy_size = OT_PAGE_SIZE <= remaining ? OT_PAGE_SIZE : remaining;

      // Fill and map the page.
      TRACE_PROC(LLOUD, "copying %d bytes to page %x from %x", copy_size, page.raw(),
                 (uintptr_t)image_or_pc + off);
      memcpy(page.as_ptr(), ((char *)image_or_pc) + off, copy_size);
      map_page(page_table.as<uintptr_t>(), (uintptr_t)USER_BASE + off,
               page, PAGE_U | PAGE_R | PAGE_W | PAGE_X, i);
    }
  }

  free_proc->page_table = page_table.as<uintptr_t>();
#else
  // WASM: No page tables needed, use direct memory access
  free_proc->page_table = nullptr;

  // For WASM, we don't support loading binary images
  // Programs must be compiled together with the kernel
  if (is_image) {
    PANIC("Binary image loading not supported on WASM");
  }
#endif

  TRACE_PROC(LSOFT, "proc %s stack ptr: %x", free_proc->name, free_proc->stack_ptr);

  memory_increment_process_count();

  return free_proc;
}

Process *process_create(const char *name, const void *image_or_pc, size_t size,
                        bool is_image) {
  Process *p =
      process_create_impl(procs, PROCS_MAX, name, image_or_pc, size, is_image);

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

  return next;
}

void process_exit(Process *proc) {
  TRACE_PROC(LSOFT, "Process %d (%s) exiting", proc->pid, proc->name);

  // Free all pages allocated to this process
  page_free_process(proc->pid);

  omemset(proc, 0, sizeof(Process));
  proc->state = UNUSED;
}