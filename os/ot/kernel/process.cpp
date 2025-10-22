#include "ot/gen/user-prog-sections.h"
#include "ot/kernel/kernel.hpp"
#include "ot/shared/mpack-writer.hpp"
#include "ot/shared/page-allocator.hpp"

extern char __kernel_base[];

Process procs[PROCS_MAX];

Process *current_proc = nullptr, *idle_proc = nullptr;

PageInfo *user_text_pages = nullptr, *user_rodata_pages = nullptr;

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
                             size_t size, bool is_image, Arguments *args) {
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
    map_page(page_table.as<uintptr_t>(), paddr, PageAddr(paddr),
             PAGE_R | PAGE_W | PAGE_X, i);

  // Map user pages.
  if (is_image) {
    TRACE_PROC(LLOUD, "found image. allocating pages");
    if (user_text_pages) {
    }
    for (size_t off = 0; off < size; off += OT_PAGE_SIZE) {
      uintptr_t virtual_addr = USER_BASE + off;
      uint32_t flags = 0;

      // Use appropriate
      if (virtual_addr >= PROG_TEXT_START && virtual_addr <= PROG_TEXT_END) {
        flags = PAGE_U | PAGE_R | PAGE_X;
        // TRACE_PROC(LSOFT, "allocating text page");
      } else if (virtual_addr >= PROG_RODATA_START &&
                 virtual_addr <= PROG_RODATA_END) {
        flags = PAGE_U | PAGE_R | PAGE_X;
        // TRACE_PROC(LSOFT, "allocating rodata page");
      } else if (virtual_addr >= PROG_DATA_START &&
                 virtual_addr <= PROG_DATA_END) {
        flags = PAGE_U | PAGE_R | PAGE_W | PAGE_X;
        // TRACE_PROC(LSOFT, "allocating data or bss page");
      } else if (virtual_addr >= PROG_BSS_START) {
        // TRACE_PROC(LSOFT, "allocating bss page");
        flags = PAGE_U | PAGE_R | PAGE_W | PAGE_X;
      }

      PageAddr page = page_allocate(i, 1);

      // Handle the case where the data to be copied is smaller than the
      // page size.
      size_t remaining = size - off;
      size_t copy_size = OT_PAGE_SIZE <= remaining ? OT_PAGE_SIZE : remaining;

      // Fill and map the page.
      TRACE_PROC(LLOUD, "copying %d bytes to page %x from %x", copy_size,
                 page.raw(), (uintptr_t)image_or_pc + off);
      memcpy(page.as_ptr(), ((char *)image_or_pc) + off, copy_size);
      map_page(page_table.as<uintptr_t>(), virtual_addr, page, flags, i);
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

  Pair<PageAddr, PageAddr> comm_page =
      process_alloc_mapped_page(free_proc, true, true, false);
  if (comm_page.first.is_null() || comm_page.second.is_null()) {
    PANIC("failed to allocate comm page");
  }

  MPackWriter msg(comm_page.first.as<char>(), OT_PAGE_SIZE);
  msg.nil();

  free_proc->comm_page = comm_page;
  TRACE_PROC(LSOFT, "allocated comm page with paddr %x and vaddr %x",
             free_proc->comm_page.first.raw(),
             free_proc->comm_page.second.raw());

  // Handle argument array
  if (args) {

    Pair<PageAddr, PageAddr> alloc_result =
        process_alloc_mapped_page(free_proc, true, false, false);

    TRACE_PROC(
        LSOFT,
        "allocating argument page for %d arguments with paddr %x and vaddr %x",
        args->argc, alloc_result.first.raw(), alloc_result.second.raw());

    char *buffer = alloc_result.first.as<char>();

    MPackWriter msg(buffer, OT_PAGE_SIZE);

    msg.map(1).str("args").stringarray(args->argc, args->argv);

    // Store virtual address for user access
    free_proc->arg_page = alloc_result.second;
  }
  // }

  TRACE_PROC(LSOFT, "proc %s stack ptr: %x", free_proc->name,
             free_proc->stack_ptr);

  memory_increment_process_count();

  return free_proc;
}

Process *process_create(const char *name, const void *image_or_pc, size_t size,
                        bool is_image, Arguments *args) {
  Process *p = process_create_impl(procs, PROCS_MAX, name, image_or_pc, size,
                                   is_image, args);

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

PageAddr process_get_arg_page() {
  PageAddr paddr = PageAddr(nullptr);
  if (current_proc == nullptr) {
    return paddr;
  }
  return current_proc->arg_page;
}

Pair<PageAddr, PageAddr> process_get_comm_page(void) {
  if (current_proc == nullptr) {
    return make_pair(PageAddr(nullptr), PageAddr(nullptr));
  }
  return current_proc->comm_page;
}

Pair<PageAddr, PageAddr> process_alloc_mapped_page(Process *proc, bool readable,
                                                   bool writable,
                                                   bool executable) {
  if (!proc) {
    return make_pair(PageAddr(nullptr), PageAddr(nullptr));
  }

  // Allocate physical page
  PageAddr paddr = page_allocate(proc->pid, 1);
  if (paddr.is_null()) {
    return make_pair(PageAddr(nullptr), PageAddr(nullptr));
  }

#ifndef OT_ARCH_WASM
  // RISC-V: Use MMU to map page to virtual address with specified permissions
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