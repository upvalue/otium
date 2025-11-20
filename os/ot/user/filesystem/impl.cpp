#include "ot/user/gen/filesystem-server.hpp"

Result<uintptr_t, ErrorCode> FilesystemServer::handle_open(const ou::string& path, uintptr_t flags) {
  // TODO: Implement
  return Result<uintptr_t, ErrorCode>::ok(0);
}

Result<uintptr_t, ErrorCode> FilesystemServer::handle_read(uintptr_t handle, uintptr_t offset, uintptr_t length) {
  // TODO: Implement
  return Result<uintptr_t, ErrorCode>::ok(0);
}

Result<uintptr_t, ErrorCode> FilesystemServer::handle_write(uintptr_t handle, uintptr_t offset, const ou::vector<uint8_t>& data) {
  // TODO: Implement
  return Result<uintptr_t, ErrorCode>::ok(0);
}

Result<bool, ErrorCode> FilesystemServer::handle_close(uintptr_t handle) {
  // TODO: Implement
  return Result<bool, ErrorCode>::ok(true);
}

Result<uintptr_t, ErrorCode> FilesystemServer::handle_read_all(const ou::string& path) {
  // TODO: Implement
  return Result<uintptr_t, ErrorCode>::ok(0);
}

Result<bool, ErrorCode> FilesystemServer::handle_write_all(const ou::string& path, const ou::vector<uint8_t>& data) {
  // TODO: Implement
  return Result<bool, ErrorCode>::ok(true);
}

Result<bool, ErrorCode> FilesystemServer::handle_create_dir(const ou::string& path) {
  // TODO: Implement
  return Result<bool, ErrorCode>::ok(true);
}

Result<bool, ErrorCode> FilesystemServer::handle_delete_file(const ou::string& path) {
  // TODO: Implement
  return Result<bool, ErrorCode>::ok(true);
}

Result<bool, ErrorCode> FilesystemServer::handle_delete_dir(const ou::string& path) {
  // TODO: Implement
  return Result<bool, ErrorCode>::ok(true);
}

void proc_filesystem(void) {
  FilesystemServer server;
  server.run();
}
