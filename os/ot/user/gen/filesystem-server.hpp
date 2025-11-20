#pragma once
#include "ot/lib/ipc.hpp"
#include "ot/lib/result.hpp"
#include "ot/lib/error-codes.hpp"
#include "ot/user/gen/filesystem-types.hpp"
#include "ot/user/gen/server-base.hpp"
#include "ot/user/string.hpp"
#include "ot/user/vector.hpp"

struct FilesystemServer : ServerBase {
  // Methods to implement
  Result<uintptr_t, ErrorCode> handle_open(const ou::string& path, uintptr_t flags);
  Result<uintptr_t, ErrorCode> handle_read(uintptr_t handle, uintptr_t offset, uintptr_t length);
  Result<uintptr_t, ErrorCode> handle_write(uintptr_t handle, uintptr_t offset, const ou::vector<uint8_t>& data);
  Result<bool, ErrorCode> handle_close(uintptr_t handle);
  Result<uintptr_t, ErrorCode> handle_read_all(const ou::string& path);
  Result<bool, ErrorCode> handle_write_all(const ou::string& path, const ou::vector<uint8_t>& data);
  Result<bool, ErrorCode> handle_create_dir(const ou::string& path);
  Result<bool, ErrorCode> handle_delete_file(const ou::string& path);
  Result<bool, ErrorCode> handle_delete_dir(const ou::string& path);

  // Framework methods - handles dispatch
  void process_request(const IpcMessage& msg);
  void run();
};
