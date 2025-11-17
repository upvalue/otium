// kernel-tests.cpp - Kernel test programs and test entry point

#include "ot/core/kernel.hpp"
#include "ot/user/user.hpp"

// Include generated IPC code for codegen test
#if KERNEL_PROG == KERNEL_PROG_TEST_IPC_CODEGEN
#include "ot/user/gen/fibonacci-client.hpp"
#endif

// a basic process that just prints hello world and exits
void proc_hello_world(void) {
  oprintf("TEST: Hello, world!\n");
  // For now, loop forever - we'll add proper exit via syscall later
  while (1) {
    current_proc->state = TERMINATED;
    yield();
  }
}

// Memory test now uses proc_mem_test function pointer (no binary image needed)

// Test process for memory recycling - just does minimal work and exits
void proc_mem_test(void) {
  oprintf("TEST: Process %d running\n", current_proc->pid);
  current_proc->state = TERMINATED;
  yield();
}

// TEST_ALTERNATE: Process A - outputs 1, yields, outputs 3
void proc_alternate_a(void) {
  oputchar('1');
  yield();
  oputchar('3');
  yield();
  current_proc->state = TERMINATED;
  yield();
}

// TEST_ALTERNATE: Process B - outputs 2, yields, outputs 4
void proc_alternate_b(void) {
  oputchar('2');
  yield();
  oputchar('4');
  yield();
  current_proc->state = TERMINATED;
  yield();
}

// TEST_USERSPACE: Simple userspace demo
// Tests basic user mode execution with proper syscalls
void proc_userspace_demo(void) {
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
void proc_fibonacci_service(void) {
  oprintf("TEST: Fibonacci service started\n");
  while (true) {
    IpcMessage msg = ou_ipc_recv();
    intptr_t method = IPC_UNPACK_METHOD(msg.method_and_flags);
    TRACE_IPC(LSOFT, "Fibonacci service received request: method=%d, arg=%d", method, msg.args[0]);

    IpcResponse resp = {NONE, {0, 0, 0}};
    if (method == 0 && msg.args[0] >= 0) {
      resp.values[0] = calculate_fibonacci(msg.args[0]);
      oprintf("TEST: Calculated fib(%d) = %d\n", msg.args[0], resp.values[0]);
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
void proc_ipc_client(void) {
  ou_yield(); // Let service start first

  int fib_pid = ou_proc_lookup("fibonacci");
  oprintf("TEST: Client found fibonacci service at PID %d\n", fib_pid);

  int test_values[] = {5, 10, 15};
  for (int i = 0; i < 3; i++) {
    int val = test_values[i];
    oprintf("TEST: Client requesting fib(%d)\n", val);
    IpcResponse resp = ou_ipc_send(fib_pid, IPC_FLAG_NONE, 0, val, 0, 0);
    if (resp.error_code == NONE) {
      oprintf("TEST: Client received result: %d\n", resp.values[0]);
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

// Sentinel variable for dummy PID 1 process
static volatile bool ipc_ordering_test_complete = false;

// TEST_IPC_ORDERING: Process 1 (Dummy) - Keeps PID 1 alive until test completes
void proc_dummy_pid1(void) {
  while (!ipc_ordering_test_complete) {
    ou_yield();
  }
  ou_exit();
}

// TEST_IPC_ORDERING: Process 2 - Client that sends IPC to echo server
void proc_ipc_client_ordering(void) {
  oprintf("TEST: Process 2 starting\n");

  // Yield to let other processes initialize (especially echo server needs to enter IPC_WAIT)
  ou_yield();

  // Look up echo server by name
  int echo_pid = ou_proc_lookup("echo_server");

  if (echo_pid == 0) {
    oprintf("TEST: Failed to find echo server\n");
    ou_exit();
  }

  IpcResponse resp = ou_ipc_send(echo_pid, IPC_FLAG_NONE, 0, 42, 0, 0);
  if (resp.error_code == NONE) {
    oprintf("TEST: %d\n", resp.values[0]);
  } else {
    oprintf("TEST: IPC error %d\n", resp.error_code);
  }

  // Signal that test is complete so dummy PID 1 can exit
  ipc_ordering_test_complete = true;
  ou_exit();
}

// TEST_IPC_ORDERING: Process 3 - Echo server that handles one IPC request then terminates
void proc_ipc_echo_once(void) {
  // First time through: wait for IPC, handle it, reply
  IpcMessage msg = ou_ipc_recv(); // Will block in IPC_WAIT
  oprintf("TEST: Process 3 handling IPC request\n");
  IpcResponse resp = {NONE, {msg.args[0], 0, 0}}; // Echo the value
  ou_ipc_reply(resp);

  // After reply returns, we're back here and continue execution
  // Now just terminate
  oprintf("TEST: Process 3 done with IPC, terminating\n");
  ou_exit();
}

// TEST_IPC_ORDERING: Process 4 - Simple test process
void proc_test_4(void) {
  oprintf("TEST: Test process 4\n");
  ou_exit();
}

void kernel_prog_test_ipc_ordering() {
  oprintf("TEST: Starting IPC ordering test\n");

  // Create processes with dummy PID 1 to avoid early kernel exit
  // PIDs will be: dummy=1, ipc_client=2, echo_server=3, test_4=4
  process_create("dummy", (const void *)proc_dummy_pid1, nullptr, false);
  process_create("ipc_client", (const void *)proc_ipc_client_ordering, nullptr, false);
  process_create("echo_server", (const void *)proc_ipc_echo_once, nullptr, false);
  process_create("test_4", (const void *)proc_test_4, nullptr, false);
}

// TEST_IPC_CODEGEN: Client using generated FibonacciClient wrapper
#if KERNEL_PROG == KERNEL_PROG_TEST_IPC_CODEGEN
void proc_ipc_codegen_client(void) {
  ou_yield(); // Let server start first

  int fib_pid = ou_proc_lookup("fibonacci");
  oprintf("TEST: Client found fibonacci service at PID %d\n", fib_pid);

  FibonacciClient client(fib_pid);

  // Test calc_fib with single return value
  int test_values[] = {5, 10, 15};
  for (int i = 0; i < 3; i++) {
    int val = test_values[i];
    oprintf("TEST: Client requesting calc_fib(%d)\n", val);

    auto result = client.calc_fib(val);
    if (result.is_ok()) {
      oprintf("TEST: Client received result: %ld\n", result.value());
    } else {
      oprintf("TEST: Client got error %d\n", result.error());
    }
  }

  // Test calc_pair with multiple return values
  oprintf("TEST: Client requesting calc_pair(7, 8)\n");
  auto pair_result = client.calc_pair(7, 8);
  if (pair_result.is_ok()) {
    auto val = pair_result.value();
    oprintf("TEST: Client received fib(7)=%ld, fib(8)=%ld\n", val.fib_n, val.fib_m);
  } else {
    oprintf("TEST: Client got error %d\n", pair_result.error());
  }

  // Test get_cache_size with unsigned return
  oprintf("TEST: Client requesting get_cache_size()\n");
  auto cache_result = client.get_cache_size();
  if (cache_result.is_ok()) {
    oprintf("TEST: Cache size: %lu\n", cache_result.value());
  } else {
    oprintf("TEST: Client got error %d\n", cache_result.error());
  }

  // Test error handling
  oprintf("TEST: Client requesting calc_fib(50) - should fail\n");
  auto error_result = client.calc_fib(50);
  if (error_result.is_err()) {
    oprintf("TEST: Got expected error: %d (%s)\n",
            error_result.error(), error_code_to_string(error_result.error()));
  } else {
    oprintf("TEST: ERROR - Should have received error but got: %ld\n", error_result.value());
  }

  // Shutdown the server cleanly
  oprintf("TEST: Client sending shutdown to server\n");
  auto shutdown_result = client.shutdown();
  if (shutdown_result.is_ok()) {
    oprintf("TEST: Server shutdown initiated\n");
  } else {
    oprintf("TEST: Shutdown failed with error %d\n", shutdown_result.error());
  }

  oprintf("TEST: IPC codegen test complete\n");
  ou_exit();
}

void kernel_prog_test_ipc_codegen() {
  oprintf("TEST: Starting IPC codegen test (using generated client/server)\n");

  // proc_fibonacci is defined in ot/user/fibonacci/impl.cpp
  extern void proc_fibonacci(void);

  Process *proc_fib = process_create("fibonacci", (const void *)proc_fibonacci, nullptr, false);
  Process *proc_client = process_create("client", (const void *)proc_ipc_codegen_client, nullptr, false);
  TRACE(LSOFT, "created fibonacci server with name %s and pid %d", proc_fib->name, proc_fib->pid);
  TRACE(LSOFT, "created codegen client with name %s and pid %d", proc_client->name, proc_client->pid);
}
#endif

/**
 * Single entry point for all kernel tests
 */
void kernel_prog_test() {
  oprintf("kernel_prog_test: KERNEL_PROG = %d\n", KERNEL_PROG);

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
#elif KERNEL_PROG == KERNEL_PROG_TEST_IPC_ORDERING
  kernel_prog_test_ipc_ordering();
#elif KERNEL_PROG == KERNEL_PROG_TEST_IPC_CODEGEN
  kernel_prog_test_ipc_codegen();
#endif
}
