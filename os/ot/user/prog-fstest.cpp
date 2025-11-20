// prog-fstest.cpp - Filesystem test program
#include "ot/lib/messages.hpp"
#include "ot/user/gen/filesystem-client.hpp"
#include "ot/user/local-storage.hpp"
#include "ot/user/string.hpp"
#include "ot/user/vector.hpp"
#include "ot/user/user.hpp"
#include "ot/lib/mpack/mpack-reader.hpp"
#include "ot/user/prog.h"

// Test storage
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

void fstest_main() {
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

    oprintf("DEBUG: About to call write_all\n");
    auto result = client.write_all(path, data);
    oprintf("DEBUG: write_all returned\n");
    if (result.is_err()) {
      oprintf("ERROR: write_all failed with error code: %d\n", result.error());
    }
    TEST_ASSERT(result.is_ok(), "Failed to write file");
    oprintf("DEBUG: write_all succeeded\n");
  }

  // Test 3: Read the file back using read_all
  TEST_PRINT("Test 3: Reading file /testdir/hello.txt");
  {
    ou::string path = "/testdir/hello.txt";
    auto result = client.read_all(path);
    TEST_ASSERT(result.is_ok(), "Failed to read file");

    uintptr_t size = result.value();
    oprintf("  Size: %lu bytes\n", size);
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
    oprintf("  Opened file, handle: %lu\n", handle.raw());

    // Write some data
    ou::vector<uint8_t> write_data;
    for (int i = 0; i < 100; i++) {
      write_data.push_back(static_cast<uint8_t>(i));
    }

    auto write_result = client.write(handle, 0, write_data);
    TEST_ASSERT(write_result.is_ok(), "Failed to write data");
    oprintf("  Wrote %lu bytes\n", write_result.value());

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
