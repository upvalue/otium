// kernel-tests.cpp - Kernel test programs and test entry point

#include "ot/core/kernel.hpp"
#include "ot/user/user.hpp"

// Include generated IPC code for codegen test
#if KERNEL_PROG == KERNEL_PROG_TEST_IPC_CODEGEN
#include "ot/user/gen/fibonacci-client.hpp"
#endif

// Include generated IPC code for graphics test
#if KERNEL_PROG == KERNEL_PROG_TEST_GRAPHICS
#include "ot/user/gen/graphics-client.hpp"
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

  Pid fib_pid = ou_proc_lookup("fibonacci");
  oprintf("TEST: Client found fibonacci service at PID %lu\n", fib_pid.raw());

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
void get_process_pages(Pidx pidx, uintptr_t *pages, uint32_t *count) {
  extern PageInfo *page_infos;
  extern uint32_t total_page_count;

  *count = 0;
  for (uint32_t i = 0; i < total_page_count && *count < 16; i++) {
    if (page_infos[i].pidx == pidx) {
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
  get_process_pages(proc1->pidx, proc1_pages, &proc1_page_count);
  oprintf("TEST: Process 1 (pidx %d, pid %lu) allocated %d pages\n", proc1->pidx, proc1->pid, proc1_page_count);

  // Create second process (kernel mode)
  Process *proc2 = process_create("mem_test_2", (const void *)proc_mem_test, nullptr, true);
  uintptr_t proc2_pages[16];
  uint32_t proc2_page_count = 0;
  get_process_pages(proc2->pidx, proc2_pages, &proc2_page_count);
  oprintf("TEST: Process 2 (pidx %d, pid %lu) allocated %d pages\n", proc2->pidx, proc2->pid, proc2_page_count);

  // Exit process 1 to free its pages
  process_exit(proc1);
  oprintf("TEST: Exited process 1 (freed %d pages)\n", proc1_page_count);

  // Create third process - should reuse process 1's pages (kernel mode)
  Process *proc3 = process_create("mem_test_3", (const void *)proc_mem_test, nullptr, true);
  uintptr_t proc3_pages[16];
  uint32_t proc3_page_count = 0;
  get_process_pages(proc3->pidx, proc3_pages, &proc3_page_count);
  oprintf("TEST: Process 3 (pidx %d, pid %lu) allocated %d pages\n", proc3->pidx, proc3->pid, proc3_page_count);

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
  TRACE(LSOFT, "created proc_a with name %s, pidx %d, pid %lu", proc_a->name, proc_a->pidx, proc_a->pid);
  TRACE(LSOFT, "created proc_b with name %s, pidx %d, pid %lu", proc_b->name, proc_b->pidx, proc_b->pid);
  oprintf("TEST: ");
}

void kernel_prog_test_userspace() {
  oprintf("TEST: Starting userspace demo test\n");
  Process *demo_proc = process_create("userspace_demo", (const void *)proc_userspace_demo, nullptr, false);
  TRACE(LSOFT, "created demo proc with name %s, pidx %d, pid %lu", demo_proc->name, demo_proc->pidx, demo_proc->pid);
}

void kernel_prog_test_ipc() {
  oprintf("TEST: Starting IPC test\n");
  Process *proc_fib = process_create("fibonacci", (const void *)proc_fibonacci_service, nullptr, false);
  Process *proc_client = process_create("client", (const void *)proc_ipc_client, nullptr, false);
  TRACE(LSOFT, "created fibonacci service with name %s, pidx %d, pid %lu", proc_fib->name, proc_fib->pidx, proc_fib->pid);
  TRACE(LSOFT, "created client with name %s, pidx %d, pid %lu", proc_client->name, proc_client->pidx, proc_client->pid);
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
  Pid echo_pid = ou_proc_lookup("echo_server");

  if (echo_pid == PID_NONE) {
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
  TRACE(LSOFT, "created fibonacci server with name %s, pidx %d, pid %lu", proc_fib->name, proc_fib->pidx, proc_fib->pid);
  TRACE(LSOFT, "created codegen client with name %s, pidx %d, pid %lu", proc_client->name, proc_client->pidx, proc_client->pid);
}
#endif

// TEST_GRAPHICS: Client that draws test pattern on framebuffer
#if KERNEL_PROG == KERNEL_PROG_TEST_GRAPHICS
void proc_graphics_client(void) {
  oprintf("TEST: Graphics client starting\n");

  // Yield to let driver initialize
  ou_yield();

  // Look up graphics driver
  Pid gfx_pid = ou_proc_lookup("graphics");
  if (gfx_pid == PID_NONE) {
    oprintf("TEST: Failed to find graphics driver\n");
    ou_exit();
  }
  oprintf("TEST: Found graphics driver at PID %lu\n", gfx_pid.raw());

  GraphicsClient client(gfx_pid);

  // Get framebuffer info
  auto fb_result = client.get_framebuffer();
  if (fb_result.is_err()) {
    oprintf("TEST: Failed to get framebuffer: %d\n", fb_result.error());
    ou_exit();
  }

  auto fb_info = fb_result.value();
  uint32_t *fb = (uint32_t *)fb_info.fb_ptr;
  uint32_t width = (uint32_t)fb_info.width;
  uint32_t height = (uint32_t)fb_info.height;

  oprintf("TEST: Got framebuffer at 0x%lx, %lux%lu\n", fb_info.fb_ptr, fb_info.width, fb_info.height);

  // Clear to black
  for (uint32_t i = 0; i < width * height; i++) {
    fb[i] = 0xFF000000; // Black in BGRA
  }

  // Draw red square in top-left (4x4)
  for (uint32_t y = 0; y < 4 && y < height; y++) {
    for (uint32_t x = 0; x < 4 && x < width; x++) {
      fb[y * width + x] = 0xFFFF0000; // Red in BGRA
    }
  }

  // Draw green square in top-right (4x4)
  for (uint32_t y = 0; y < 4 && y < height; y++) {
    for (uint32_t x = 0; x < 4 && x < width; x++) {
      if (width >= 4 + x) {
        fb[y * width + (width - 4 + x)] = 0xFF00FF00; // Green in BGRA
      }
    }
  }

  // Draw blue square in bottom-left (4x4)
  for (uint32_t y = 0; y < 4 && y < height; y++) {
    for (uint32_t x = 0; x < 4 && x < width; x++) {
      if (height >= 4 + y) {
        fb[(height - 4 + y) * width + x] = 0xFF0000FF; // Blue in BGRA
      }
    }
  }

  // Draw white square in center (2x2)
  uint32_t center_x = width / 2 - 1;
  uint32_t center_y = height / 2 - 1;
  for (uint32_t y = 0; y < 2; y++) {
    for (uint32_t x = 0; x < 2; x++) {
      fb[(center_y + y) * width + (center_x + x)] = 0xFFFFFFFF; // White in BGRA
    }
  }

  oprintf("TEST: Drew test pattern\n");

  // Flush to display
  auto flush_result = client.flush();
  if (flush_result.is_ok()) {
    oprintf("TEST: Flushed framebuffer\n");
  } else {
    oprintf("TEST: Flush failed: %d\n", flush_result.error());
  }

  oprintf("TEST: Graphics test complete\n");
  ou_exit();
}

void kernel_prog_test_graphics() {
  oprintf("TEST: Starting graphics test\n");

  // proc_graphics is defined in ot/user/graphics/impl.cpp
  extern void proc_graphics(void);

  Process *driver = process_create("graphics", (const void *)proc_graphics, nullptr, false);
  Process *client = process_create("gfx_client", (const void *)proc_graphics_client, nullptr, false);
  TRACE(LSOFT, "created graphics driver with name %s, pidx %d, pid %lu", driver->name, driver->pidx, driver->pid);
  TRACE(LSOFT, "created graphics client with name %s, pidx %d, pid %lu", client->name, client->pidx, client->pid);
}
#endif

// TEST_FILESYSTEM: Client that runs filesystem tests
#if KERNEL_PROG == KERNEL_PROG_TEST_FILESYSTEM
#include "ot/user/gen/filesystem-client.hpp"
#include "ot/user/local-storage.hpp"
#include "ot/lib/mpack/mpack-reader.hpp"

// Test storage for filesystem client
struct FsTestStorage : public LocalStorage {
  FsTestStorage() {
    process_storage_init(5);  // 20KB for test data
  }
};

#define TEST_PRINT(msg) oprintf("TEST: %s\n", msg)
#define TEST_ASSERT(cond, msg) do { \
  if (!(cond)) { \
    oprintf("TEST FAILED: %s\n", msg); \
    ou_exit(); \
  } \
} while(0)

void proc_filesystem_test_client(void) {
  TEST_PRINT("Filesystem test starting");

  // Initialize storage
  void *storage_page = ou_get_storage().as_ptr();
  FsTestStorage *s = new (storage_page) FsTestStorage();

  // Yield to let filesystem initialize
  ou_yield();

  // Look up filesystem service
  Pid fs_pid = ou_proc_lookup("filesystem");
  if (fs_pid == PID_NONE) {
    TEST_PRINT("Failed to find filesystem service");
    ou_exit();
  }
  TEST_PRINT("Found filesystem service");

  FilesystemClient client(fs_pid);

  // Test 1: Create directory
  TEST_PRINT("Test 1: Creating directory /testdir");
  {
    ou::string path = "/testdir";
    auto result = client.create_dir(path);
    TEST_ASSERT(result.is_ok(), "Failed to create directory");
  }

  // Test 2: Write a small file using write_all
  TEST_PRINT("Test 2: Writing file /testdir/hello.txt");
  {
    ou::string path = "/testdir/hello.txt";
    ou::string content = "Hello, filesystem!";

    ou::vector<uint8_t> data;
    for (size_t i = 0; i < content.length(); i++) {
      data.push_back(static_cast<uint8_t>(content[i]));
    }

    auto result = client.write_all(path, data);
    TEST_ASSERT(result.is_ok(), "Failed to write file");
  }

  // Test 3: Read the file back using read_all
  TEST_PRINT("Test 3: Reading file /testdir/hello.txt");
  {
    ou::string path = "/testdir/hello.txt";
    auto result = client.read_all(path);
    TEST_ASSERT(result.is_ok(), "Failed to read file");

    uintptr_t size = result.value();
    TEST_ASSERT(size == 18, "File size mismatch");

    // Read data from comm page
    PageAddr comm = ou_get_comm_page();
    MPackReader reader(comm.as_ptr(), OT_PAGE_SIZE);

    StringView content_view;
    reader.read_bin(content_view);

    TEST_ASSERT(content_view.len == 18, "Content length mismatch");

    // Verify content
    const char* expected = "Hello, filesystem!";
    bool match = true;
    for (size_t i = 0; i < content_view.len; i++) {
      if (content_view.ptr[i] != expected[i]) {
        match = false;
        break;
      }
    }
    TEST_ASSERT(match, "Content mismatch");
    TEST_PRINT("Content verified!");
  }

  // Test 4: Handle-based file operations
  TEST_PRINT("Test 4: Testing handle-based operations");
  {
    ou::string path = "/testdir/data.bin";

    // Open file for writing (create)
    auto open_result = client.open(path, 0x04 | 0x02);  // CREATE | WRITE
    TEST_ASSERT(open_result.is_ok(), "Failed to open file for writing");
    FileHandleId handle = open_result.value();

    // Write some data
    ou::vector<uint8_t> write_data;
    for (int i = 0; i < 100; i++) {
      write_data.push_back(static_cast<uint8_t>(i));
    }

    auto write_result = client.write(handle, 0, write_data);
    TEST_ASSERT(write_result.is_ok(), "Failed to write data");

    // Close file
    auto close_result = client.close(handle);
    TEST_ASSERT(close_result.is_ok(), "Failed to close file");
    TEST_PRINT("File closed");

    // Re-open for reading
    auto read_open_result = client.open(path, 0x01);  // READ
    TEST_ASSERT(read_open_result.is_ok(), "Failed to open file for reading");
    handle = read_open_result.value();

    // Read data back
    auto read_result = client.read(handle, 0, 100);
    TEST_ASSERT(read_result.is_ok(), "Failed to read data");
    uintptr_t bytes_read = read_result.value();
    TEST_ASSERT(bytes_read == 100, "Read size mismatch");

    // Verify data from comm page
    PageAddr comm = ou_get_comm_page();
    MPackReader reader(comm.as_ptr(), OT_PAGE_SIZE);

    StringView data_view;
    reader.read_bin(data_view);

    bool data_match = true;
    for (size_t i = 0; i < 100; i++) {
      if (static_cast<uint8_t>(data_view.ptr[i]) != static_cast<uint8_t>(i)) {
        data_match = false;
        break;
      }
    }
    TEST_ASSERT(data_match, "Data verification failed");
    TEST_PRINT("Data verified!");

    // Close file
    client.close(handle);
  }

  // Test 5: Create nested directories
  TEST_PRINT("Test 5: Creating nested directory");
  {
    ou::string path = "/testdir/subdir";
    auto result = client.create_dir(path);
    TEST_ASSERT(result.is_ok(), "Failed to create nested directory");
  }

  // Test 6: Write file in nested directory
  TEST_PRINT("Test 6: Writing to nested directory");
  {
    ou::string path = "/testdir/subdir/nested.txt";
    ou::string content = "Nested!";

    ou::vector<uint8_t> data;
    for (size_t i = 0; i < content.length(); i++) {
      data.push_back(static_cast<uint8_t>(content[i]));
    }

    auto result = client.write_all(path, data);
    TEST_ASSERT(result.is_ok(), "Failed to write to nested directory");
  }

  // Test 7: Read from nested directory
  TEST_PRINT("Test 7: Reading from nested directory");
  {
    ou::string path = "/testdir/subdir/nested.txt";
    auto result = client.read_all(path);
    TEST_ASSERT(result.is_ok(), "Failed to read from nested directory");
    TEST_ASSERT(result.value() == 7, "Nested file size mismatch");
  }

  // Test 8: Delete file
  TEST_PRINT("Test 8: Deleting file");
  {
    ou::string path = "/testdir/hello.txt";
    auto result = client.delete_file(path);
    TEST_ASSERT(result.is_ok(), "Failed to delete file");

    // Verify file is gone
    auto read_result = client.read_all(path);
    TEST_ASSERT(read_result.is_err(), "File should not exist after deletion");
  }

  // Test 9: Error handling - file not found
  TEST_PRINT("Test 9: Testing error handling");
  {
    ou::string path = "/nonexistent.txt";
    auto result = client.read_all(path);
    TEST_ASSERT(result.is_err(), "Should fail for nonexistent file");
    TEST_ASSERT(result.error() == FILESYSTEM__FILE_NOT_FOUND, "Wrong error code");
  }

  TEST_PRINT("===========================================");
  TEST_PRINT("ALL FILESYSTEM TESTS PASSED!");
  TEST_PRINT("===========================================");

  ou_exit();
}

void kernel_prog_test_filesystem() {
  oprintf("TEST: Starting filesystem test\n");

  // proc_filesystem is defined in ot/user/filesystem/impl.cpp
  extern void proc_filesystem(void);

  Process *fs_server = process_create("filesystem", (const void *)proc_filesystem, nullptr, false);
  Process *test_client = process_create("fs_test_client", (const void *)proc_filesystem_test_client, nullptr, false);
  TRACE(LSOFT, "created filesystem server with name %s, pidx %d, pid %lu", fs_server->name, fs_server->pidx, fs_server->pid);
  TRACE(LSOFT, "created filesystem test client with name %s, pidx %d, pid %lu", test_client->name, test_client->pidx, test_client->pid);
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
  TRACE(LSOFT, "created test proc with name %s, pidx %d, pid %lu", test_proc->name, test_proc->pidx, test_proc->pid);
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
#elif KERNEL_PROG == KERNEL_PROG_TEST_GRAPHICS
  kernel_prog_test_graphics();
#elif KERNEL_PROG == KERNEL_PROG_TEST_FILESYSTEM
  kernel_prog_test_filesystem();
#endif
}
