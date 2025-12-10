#include "ot/user/gen/filesystem-server.hpp"

Result<FileHandleId, ErrorCode> FilesystemServer::handle_open(const ou::string& path, uintptr_t flags) {
  // TODO: Implement
  return Result<FileHandleId, ErrorCode>::ok(0);
}

Result<uintptr_t, ErrorCode> FilesystemServer::handle_read(FileHandleId handle, uintptr_t offset, uintptr_t length) {
  // TODO: Implement
  return Result<uintptr_t, ErrorCode>::ok(0);
}

Result<uintptr_t, ErrorCode> FilesystemServer::handle_write(FileHandleId handle, uintptr_t offset, const ou::vector<uint8_t>& data) {
  // TODO: Implement
  return Result<uintptr_t, ErrorCode>::ok(0);
}

Result<bool, ErrorCode> FilesystemServer::handle_close(FileHandleId handle) {
  // TODO: Implement
  return Result<bool, ErrorCode>::ok(true);
}

Result<bool, ErrorCode> FilesystemServer::handle_create_file(const ou::string& path) {
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
