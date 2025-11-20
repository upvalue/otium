#include "ot/user/gen/filesystem-client.hpp"
#include "ot/user/gen/method-ids.hpp"
#include "ot/user/user.hpp"
#include "ot/lib/mpack/mpack-reader.hpp"

Result<FileHandleId, ErrorCode> FilesystemClient::open(const ou::string& path, uintptr_t flags) {
  // Serialize complex arguments to comm page
  CommWriter writer;
  writer.writer().str(path.c_str());

  IpcResponse resp = ou_ipc_send(
    pid_,
    IPC_FLAG_SEND_COMM_DATA,
    MethodIds::Filesystem::OPEN,
    flags, 0, 0  );

  if (resp.error_code != NONE) {
    return Result<FileHandleId, ErrorCode>::err(resp.error_code);
  }

  // Convert raw IPC value to typed value
  return Result<FileHandleId, ErrorCode>::ok(FileHandleId(resp.values[0]));
}

Result<uintptr_t, ErrorCode> FilesystemClient::read(FileHandleId handle, uintptr_t offset, uintptr_t length) {

  IpcResponse resp = ou_ipc_send(
    pid_,
    0 | IPC_FLAG_RECV_COMM_DATA,
    MethodIds::Filesystem::READ,
    handle.raw(), offset, length  );

  if (resp.error_code != NONE) {
    return Result<uintptr_t, ErrorCode>::err(resp.error_code);
  }

  // Response data is in comm page - caller reads it with MPackReader
  // Return value indicates size or count
  return Result<uintptr_t, ErrorCode>::ok(resp.values[0]);
}

Result<uintptr_t, ErrorCode> FilesystemClient::write(FileHandleId handle, uintptr_t offset, const ou::vector<uint8_t>& data) {
  // Serialize complex arguments to comm page
  CommWriter writer;
  writer.writer().bin(data.data(), data.size());

  IpcResponse resp = ou_ipc_send(
    pid_,
    IPC_FLAG_SEND_COMM_DATA,
    MethodIds::Filesystem::WRITE,
    handle.raw(), offset, 0  );

  if (resp.error_code != NONE) {
    return Result<uintptr_t, ErrorCode>::err(resp.error_code);
  }

  return Result<uintptr_t, ErrorCode>::ok(resp.values[0]);
}

Result<bool, ErrorCode> FilesystemClient::close(FileHandleId handle) {

  IpcResponse resp = ou_ipc_send(
    pid_,
    0,
    MethodIds::Filesystem::CLOSE,
    handle.raw(), 0, 0  );

  if (resp.error_code != NONE) {
    return Result<bool, ErrorCode>::err(resp.error_code);
  }

  return Result<bool, ErrorCode>::ok({});
}

Result<uintptr_t, ErrorCode> FilesystemClient::read_all(const ou::string& path) {
  // Serialize complex arguments to comm page
  CommWriter writer;
  writer.writer().str(path.c_str());

  IpcResponse resp = ou_ipc_send(
    pid_,
    IPC_FLAG_SEND_COMM_DATA | IPC_FLAG_RECV_COMM_DATA,
    MethodIds::Filesystem::READ_ALL,
    0, 0, 0  );

  if (resp.error_code != NONE) {
    return Result<uintptr_t, ErrorCode>::err(resp.error_code);
  }

  // Response data is in comm page - caller reads it with MPackReader
  // Return value indicates size or count
  return Result<uintptr_t, ErrorCode>::ok(resp.values[0]);
}

Result<bool, ErrorCode> FilesystemClient::write_all(const ou::string& path, const ou::vector<uint8_t>& data) {
  // Serialize complex arguments to comm page
  CommWriter writer;
  writer.writer().str(path.c_str());
  writer.writer().bin(data.data(), data.size());

  IpcResponse resp = ou_ipc_send(
    pid_,
    IPC_FLAG_SEND_COMM_DATA,
    MethodIds::Filesystem::WRITE_ALL,
    0, 0, 0  );

  if (resp.error_code != NONE) {
    return Result<bool, ErrorCode>::err(resp.error_code);
  }

  return Result<bool, ErrorCode>::ok({});
}

Result<bool, ErrorCode> FilesystemClient::create_dir(const ou::string& path) {
  // Serialize complex arguments to comm page
  CommWriter writer;
  writer.writer().str(path.c_str());

  IpcResponse resp = ou_ipc_send(
    pid_,
    IPC_FLAG_SEND_COMM_DATA,
    MethodIds::Filesystem::CREATE_DIR,
    0, 0, 0  );

  if (resp.error_code != NONE) {
    return Result<bool, ErrorCode>::err(resp.error_code);
  }

  return Result<bool, ErrorCode>::ok({});
}

Result<bool, ErrorCode> FilesystemClient::delete_file(const ou::string& path) {
  // Serialize complex arguments to comm page
  CommWriter writer;
  writer.writer().str(path.c_str());

  IpcResponse resp = ou_ipc_send(
    pid_,
    IPC_FLAG_SEND_COMM_DATA,
    MethodIds::Filesystem::DELETE_FILE,
    0, 0, 0  );

  if (resp.error_code != NONE) {
    return Result<bool, ErrorCode>::err(resp.error_code);
  }

  return Result<bool, ErrorCode>::ok({});
}

Result<bool, ErrorCode> FilesystemClient::delete_dir(const ou::string& path) {
  // Serialize complex arguments to comm page
  CommWriter writer;
  writer.writer().str(path.c_str());

  IpcResponse resp = ou_ipc_send(
    pid_,
    IPC_FLAG_SEND_COMM_DATA,
    MethodIds::Filesystem::DELETE_DIR,
    0, 0, 0  );

  if (resp.error_code != NONE) {
    return Result<bool, ErrorCode>::err(resp.error_code);
  }

  return Result<bool, ErrorCode>::ok({});
}


Result<bool, ErrorCode> FilesystemClient::shutdown() {
  IpcResponse resp = ou_ipc_send(
    pid_,
    IPC_FLAG_NONE,
    IPC_METHOD_SHUTDOWN,
    0, 0, 0
  );

  if (resp.error_code != NONE) {
    return Result<bool, ErrorCode>::err(resp.error_code);
  }

  return Result<bool, ErrorCode>::ok(true);
}

