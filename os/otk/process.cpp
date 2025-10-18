#include "otk/kernel.hpp"

extern char __kernel_base[];

Process procs[PROCS_MAX];

Process *current_proc = nullptr, *idle_proc = nullptr;

void map_page(uintptr_t *table1, uintptr_t vaddr, uintptr_t paddr,
              uint32_t flags, uint32_t pid) {
  if (!is_aligned((void *)vaddr, PAGE_SIZE))
    PANIC("unaligned vaddr %x", vaddr);

  if (!is_aligned((void *)paddr, PAGE_SIZE))
    PANIC("unaligned paddr %x", paddr);

  uint32_t vpn1 = (vaddr >> 22) & 0x3ff;
  if ((table1[vpn1] & PAGE_V) == 0) {
    // Create the 1st level page table if it doesn't exist.
    uintptr_t pt_paddr = (uintptr_t)page_allocate(pid, 1);
    table1[vpn1] = ((pt_paddr / PAGE_SIZE) << 10) | PAGE_V;
  }

  // Set the 2nd level page table entry to map the physical page.
  uint32_t vpn0 = (vaddr >> 12) & 0x3ff;
  uint32_t *table0 = (uint32_t *)((table1[vpn1] >> 10) * PAGE_SIZE);
  table0[vpn0] = ((paddr / PAGE_SIZE) << 10) | flags | PAGE_V;
}

Process *process_create_impl(Process *table, uint32_t max_procs,
                             const char *name, const void *image_or_pc,
                             uint32_t size, bool is_image) {
  // Initialize memory tracking on first process creation
  memory_init();

  Process *free_proc = nullptr;
  uint32_t i;
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

  // Map kernel pages.
  void *page_table = page_allocate(i, 1);
  for (uintptr_t paddr = (uintptr_t)__kernel_base;
       paddr < (uintptr_t)__free_ram_end; paddr += PAGE_SIZE)
    map_page((uintptr_t *)page_table, paddr, paddr, PAGE_R | PAGE_W | PAGE_X, i);

  // Map user pages.
  if (is_image) {
    oprintf("found image. allocating pages\n");
    for (size_t off = 0; off < size; off += PAGE_SIZE) {
      void *page = page_allocate(i, 1);

      // Handle the case where the data to be copied is smaller than the
      // page size.
      size_t remaining = size - off;
      size_t copy_size = PAGE_SIZE <= remaining ? PAGE_SIZE : remaining;

      // Fill and map the page.
      oprintf("copying %d bytes to page %x from %x\n", copy_size, page,
              (uintptr_t)image_or_pc + off);
      memcpy((void *)page, ((char *)image_or_pc) + off, copy_size);
      map_page((uintptr_t *)page_table, (uintptr_t)USER_BASE + off,
               (uintptr_t)page, PAGE_U | PAGE_R | PAGE_W | PAGE_X, i);
    }
  }

  free_proc->page_table = (uintptr_t *)page_table;

  TRACE("proc %s stack ptr: %x", free_proc->name, free_proc->stack_ptr);

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
  TRACE("Process %d (%s) exiting", proc->pid, proc->name);

  // Free all pages allocated to this process
  page_free_process(proc->pid);

  omemset(proc, 0, sizeof(Process));
  proc->state = UNUSED;
}