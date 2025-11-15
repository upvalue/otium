// kernel-prog.cpp - Kernel startup logic and test programs

#include "ot/core/drivers/drv-gfx-virtio.hpp"
#include "ot/core/kernel.hpp"
#include "ot/user/user.hpp"

// Forward declaration for kernel_common (defined in startup.cpp)
void kernel_common(void);

// Forward declaration for user_program_main (defined in user-main.cpp)
extern "C" void user_program_main(void);

// a basic process that just prints hello world and exits
extern "C" void proc_hello_world(void) {
  oprintf("TEST: Hello, world!\n");
  // For now, loop forever - we'll add proper exit via syscall later
  while (1) {
    current_proc->state = TERMINATED;
  }
}

// Memory test now uses proc_mem_test function pointer (no binary image needed)

// Test process for memory recycling - just does minimal work and exits
extern "C" void proc_mem_test(void) {
  oprintf("TEST: Process %d running\n", current_proc->pid);
  current_proc->state = TERMINATED;
  yield();
}

// TEST_ALTERNATE: Process A - outputs 1, yields, outputs 3
extern "C" void proc_alternate_a(void) {
  oputchar('1');
  yield();
  oputchar('3');
  yield();
  current_proc->state = TERMINATED;
  yield();
}

// TEST_ALTERNATE: Process B - outputs 2, yields, outputs 4
extern "C" void proc_alternate_b(void) {
  oputchar('2');
  yield();
  oputchar('4');
  yield();
  current_proc->state = TERMINATED;
  yield();
}

// TEST_USERSPACE: Simple userspace demo
// Tests basic user mode execution with proper syscalls
extern "C" void proc_userspace_demo(void) {
  oprintf("TEST: Starting userspace demo\n");
  oprintf("TEST: Process running in user mode\n");
  oprintf("TEST: Testing yield syscall\n");
  ou_yield();
  oprintf("TEST: Back from yield\n");
  oprintf("TEST: SUCCESS - User mode execution works\n");
  oprintf("TEST: Terminating process\n");
  ou_exit();
}

// Simple fibonacci calculator (naive recursive)
int calculate_fibonacci(int n) {
  if (n <= 1)
    return n;
  return calculate_fibonacci(n - 1) + calculate_fibonacci(n - 2);
}

// TEST_IPC: Fibonacci service - receives IPC requests and calculates fibonacci
extern "C" void proc_fibonacci_service(void) {
  oprintf("TEST: Fibonacci service started\n");
  while (true) {
    IpcMessage msg = ou_ipc_recv();
    TRACE_IPC(LSOFT, "Fibonacci service received request: method=%d, arg=%d", msg.method, msg.extra);

    IpcResponse resp = {NONE, 0, 0};
    if (msg.method == 0 && msg.extra >= 0) {
      resp.a = calculate_fibonacci(msg.extra);
      oprintf("TEST: Calculated fib(%d) = %d\n", msg.extra, resp.a);
    } else {
      resp.error_code = IPC__METHOD_NOT_KNOWN;
      oprintf("TEST: Unknown method or negative argument\n");
    }
    ou_ipc_reply(resp);
    // After reply, we've switched back to sender and then back to here
    // Loop to receive next message
  }
}

// TEST_IPC: Client - sends fibonacci requests to the service
extern "C" void proc_ipc_client(void) {
  ou_yield(); // Let service start first

  int fib_pid = ou_proc_lookup("fibonacci");
  oprintf("TEST: Client found fibonacci service at PID %d\n", fib_pid);

  int test_values[] = {5, 10, 15};
  for (int i = 0; i < 3; i++) {
    int val = test_values[i];
    oprintf("TEST: Client requesting fib(%d)\n", val);
    IpcResponse resp = ou_ipc_send(fib_pid, 0, val);
    if (resp.error_code == NONE) {
      oprintf("TEST: Client received result: %d\n", resp.a);
    } else {
      oprintf("TEST: Client got error %d\n", resp.error_code);
    }
  }

  oprintf("TEST: IPC test complete\n");
  current_proc->state = TERMINATED;
  yield();
}

// Helper to get all pages allocated to a process
void get_process_pages(int32_t pid, uintptr_t *pages, uint32_t *count) {
  extern PageInfo *page_infos;
  extern uint32_t total_page_count;

  *count = 0;
  for (uint32_t i = 0; i < total_page_count && *count < 16; i++) {
    if (page_infos[i].pid == pid) {
      pages[(*count)++] = page_infos[i].addr.raw();
    }
  }
}

bool programs_running() {
  // start at 1 to skip idle proc
  for (int i = 1; i < PROCS_MAX; i++) {
    if (procs[i].state == RUNNABLE) {
      return true;
    }
  }
  return false;
}

void kernel_prog_test_mem() {
  // Test mode: memory recycling test
  oprintf("TEST: Starting memory recycling test\n");

  // Create first process (kernel mode)
  Process *proc1 = process_create("mem_test_1", (const void *)proc_mem_test, nullptr, true);
  uintptr_t proc1_pages[16];
  uint32_t proc1_page_count = 0;
  get_process_pages(proc1->pid, proc1_pages, &proc1_page_count);
  oprintf("TEST: Process 1 (pid %d) allocated %d pages\n", proc1->pid, proc1_page_count);

  // Create second process (kernel mode)
  Process *proc2 = process_create("mem_test_2", (const void *)proc_mem_test, nullptr, true);
  uintptr_t proc2_pages[16];
  uint32_t proc2_page_count = 0;
  get_process_pages(proc2->pid, proc2_pages, &proc2_page_count);
  oprintf("TEST: Process 2 (pid %d) allocated %d pages\n", proc2->pid, proc2_page_count);

  // Exit process 1 to free its pages
  process_exit(proc1);
  oprintf("TEST: Exited process 1 (freed %d pages)\n", proc1_page_count);

  // Create third process - should reuse process 1's pages (kernel mode)
  Process *proc3 = process_create("mem_test_3", (const void *)proc_mem_test, nullptr, true);
  uintptr_t proc3_pages[16];
  uint32_t proc3_page_count = 0;
  get_process_pages(proc3->pid, proc3_pages, &proc3_page_count);
  oprintf("TEST: Process 3 (pid %d) allocated %d pages\n", proc3->pid, proc3_page_count);

  // Verify page recycling - check if all of proc3's pages are from proc1
  uint32_t reused_count = 0;
  for (uint32_t i = 0; i < proc3_page_count; i++) {
    for (uint32_t j = 0; j < proc1_page_count; j++) {
      if (proc3_pages[i] == proc1_pages[j]) {
        reused_count++;
        break;
      }
    }
  }

  if (reused_count == proc3_page_count && proc3_page_count == proc1_page_count) {
    oprintf("TEST: SUCCESS - Process 3 reused all %d pages from Process 1\n", reused_count);
  } else {
    oprintf("TEST: FAILURE - Process 3 reused %d/%d pages (expected %d)\n", reused_count, proc3_page_count,
            proc1_page_count);
  }

  // Clean up
  process_exit(proc2);
  process_exit(proc3);
}

/**
 * tests that yielding cooperatively between processes works as expected
 */
void kernel_prog_test_alternate() {
  oprintf("TEST: Starting alternate process test (should print 1234)\n");
  Process *proc_a = process_create("alternate_a", (const void *)proc_alternate_a, nullptr, true);
  Process *proc_b = process_create("alternate_b", (const void *)proc_alternate_b, nullptr, true);
  TRACE(LSOFT, "created proc_a with name %s and pid %d", proc_a->name, proc_a->pid);
  TRACE(LSOFT, "created proc_b with name %s and pid %d", proc_b->name, proc_b->pid);
  oprintf("TEST: ");
}

void kernel_prog_test_userspace() {
  oprintf("TEST: Starting userspace demo test\n");
  Process *demo_proc = process_create("userspace_demo", (const void *)proc_userspace_demo, nullptr, false);
  TRACE(LSOFT, "created demo proc with name %s and pid %d", demo_proc->name, demo_proc->pid);
}

void kernel_prog_test_ipc() {
  oprintf("TEST: Starting IPC test\n");
  Process *proc_fib = process_create("fibonacci", (const void *)proc_fibonacci_service, nullptr, false);
  Process *proc_client = process_create("client", (const void *)proc_ipc_client, nullptr, false);
  TRACE(LSOFT, "created fibonacci service with name %s and pid %d", proc_fib->name, proc_fib->pid);
  TRACE(LSOFT, "created client with name %s and pid %d", proc_client->name, proc_client->pid);
}

/**
 * the default kernel program (actually run the system)
 */
void kernel_prog_default() {
  char *shell_argv = {"shell"};
  Arguments shell_args = {1, &shell_argv};
  Process *proc_shell = process_create("shell", (const void *)user_program_main, &shell_args, false);
}

// Kernel startup - initializes kernel and creates initial processes
void kernel_start(void) {
  kernel_common();

  // Kernel "program" there are a few different programs for testing some functionality
  // of the kernel end to end

  oprintf("kernel_start: KERNEL_PROG = %d\n", KERNEL_PROG);

#if KERNEL_PROG == KERNEL_PROG_TEST_MEM
  kernel_prog_test_mem();
#elif KERNEL_PROG == KERNEL_PROG_TEST_HELLO
  // Test mode: run hello world test (kernel mode)
  Process *test_proc = process_create("test_hello", (const void *)proc_hello_world, nullptr, true);
  TRACE(LSOFT, "created test proc with name %s and pid %d", test_proc->name, test_proc->pid);
#elif KERNEL_PROG == KERNEL_PROG_TEST_ALTERNATE
  kernel_prog_test_alternate();
#elif KERNEL_PROG == KERNEL_PROG_TEST_USERSPACE
  kernel_prog_test_userspace();
#elif KERNEL_PROG == KERNEL_PROG_TEST_IPC
  kernel_prog_test_ipc();
#else
  kernel_prog_default();
#endif

#ifdef OT_ARCH_WASM
  // For WASM: use explicit scheduler loop
  scheduler_loop();
#else
  // For RISC-V: yield and let processes run
  yield();
#endif

  OT_SOFT_ASSERT("reached end of kernel while programs were running", !programs_running());

  oputchar('\n');
  TRACE(LSOFT, "no programs left to run, exiting kernel");
  memory_report();
  kernel_exit();
}