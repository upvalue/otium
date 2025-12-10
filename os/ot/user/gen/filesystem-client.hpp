#pragma once
#include "ot/lib/ipc.hpp"
#include "ot/lib/result.hpp"
#include "ot/lib/error-codes.hpp"
#include "ot/lib/typed-int.hpp"
#include "ot/user/gen/filesystem-types.hpp"
#include "ot/user/string.hpp"
#include "ot/user/vector.hpp"

struct FilesystemClient {
  Pid pid_;

  FilesystemClient(Pid pid) : pid_(pid) {}

  Result<FileHandleId, ErrorCode> open(const ou::string& path, uintptr_t flags);
  Result<uintptr_t, ErrorCode> read(FileHandleId handle, uintptr_t offset, uintptr_t length);
  Result<uintptr_t, ErrorCode> write(FileHandleId handle, uintptr_t offset, const ou::vector<uint8_t>& data);
  Result<bool, ErrorCode> close(FileHandleId handle);
  Result<bool, ErrorCode> create_file(const ou::string& path);
  Result<bool, ErrorCode> create_dir(const ou::string& path);
  Result<bool, ErrorCode> delete_file(const ou::string& path);
  Result<bool, ErrorCode> delete_dir(const ou::string& path);
  Result<uintptr_t, ErrorCode> list_dir(const ou::string& path);

  // Universal shutdown method (sends IPC_METHOD_SHUTDOWN)
  Result<bool, ErrorCode> shutdown();
};
