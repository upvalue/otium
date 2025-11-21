#pragma once
#include "ot/lib/ipc.hpp"
#include "ot/lib/result.hpp"
#include "ot/lib/error-codes.hpp"
#include "ot/lib/typed-int.hpp"
#include "ot/lib/string-view.hpp"
#include "ot/user/gen/filesystem-types.hpp"
#include "ot/user/gen/server-base.hpp"
#include "ot/user/string.hpp"

struct FilesystemServerBase : ServerBase {
  // Virtual destructor for proper cleanup
  virtual ~FilesystemServerBase() = default;

  // Pure virtual methods to implement in derived class
  virtual Result<FileHandleId, ErrorCode> handle_open(const ou::string& path, uintptr_t flags) = 0;
  virtual Result<uintptr_t, ErrorCode> handle_read(FileHandleId handle, uintptr_t offset, uintptr_t length) = 0;
  virtual Result<uintptr_t, ErrorCode> handle_write(FileHandleId handle, uintptr_t offset, const StringView& data) = 0;
  virtual Result<bool, ErrorCode> handle_close(FileHandleId handle) = 0;
  virtual Result<uintptr_t, ErrorCode> handle_read_all(const ou::string& path) = 0;
  virtual Result<bool, ErrorCode> handle_write_all(const ou::string& path, const StringView& data) = 0;
  virtual Result<bool, ErrorCode> handle_create_dir(const ou::string& path) = 0;
  virtual Result<bool, ErrorCode> handle_delete_file(const ou::string& path) = 0;
  virtual Result<bool, ErrorCode> handle_delete_dir(const ou::string& path) = 0;

  // Framework methods - handles dispatch
  void process_request(const IpcMessage& msg);
  void run();
};
