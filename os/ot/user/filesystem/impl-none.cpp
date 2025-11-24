#include "ot/user/gen/filesystem-server.hpp"
#include "ot/user/user.hpp"

// Unimplemented filesystem - all operations return UNIMPLEMENTED error

Result<uintptr_t, ErrorCode> FilesystemServer::handle_open(const ou::string& path, uintptr_t flags) {
  return Result<uintptr_t, ErrorCode>::err(FILESYSTEM__UNIMPLEMENTED);
}

Result<uintptr_t, ErrorCode> FilesystemServer::handle_read(uintptr_t handle, uintptr_t offset, uintptr_t length) {
  return Result<uintptr_t, ErrorCode>::err(FILESYSTEM__UNIMPLEMENTED);
}

Result<uintptr_t, ErrorCode> FilesystemServer::handle_write(uintptr_t handle, uintptr_t offset, const ou::vector<uint8_t>& data) {
  return Result<uintptr_t, ErrorCode>::err(FILESYSTEM__UNIMPLEMENTED);
}

Result<bool, ErrorCode> FilesystemServer::handle_close(uintptr_t handle) {
  return Result<bool, ErrorCode>::err(FILESYSTEM__UNIMPLEMENTED);
}

Result<bool, ErrorCode> FilesystemServer::handle_create_dir(const ou::string& path) {
  return Result<bool, ErrorCode>::err(FILESYSTEM__UNIMPLEMENTED);
}

Result<bool, ErrorCode> FilesystemServer::handle_delete_file(const ou::string& path) {
  return Result<bool, ErrorCode>::err(FILESYSTEM__UNIMPLEMENTED);
}

Result<bool, ErrorCode> FilesystemServer::handle_delete_dir(const ou::string& path) {
  return Result<bool, ErrorCode>::err(FILESYSTEM__UNIMPLEMENTED);
}

void proc_filesystem(void) {
  // Unimplemented filesystem - just run the server loop
  // All operations will return UNIMPLEMENTED
  FilesystemServer server;
  server.run();
}
