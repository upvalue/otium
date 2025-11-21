#include "ot/user/gen/filesystem-server.hpp"
#include "ot/user/gen/method-ids.hpp"
#include "ot/user/user.hpp"
#include "ot/lib/mpack/mpack-reader.hpp"

void FilesystemServer::process_request(const IpcMessage& msg) {
  // Check for shutdown request (handled by base class)
  if (handle_shutdown_if_requested(msg)) {
    return;  // Server exits in base class
  }

  intptr_t method = IPC_UNPACK_METHOD(msg.method_and_flags);
  uint8_t flags = IPC_UNPACK_FLAGS(msg.method_and_flags);
  IpcResponse resp = {NONE, {0, 0, 0}};

  switch (method) {
  case MethodIds::Filesystem::OPEN: {
    // Deserialize complex arguments from comm page
    PageAddr comm = ou_get_comm_page();
    MPackReader reader(comm.as_ptr(), OT_PAGE_SIZE);
    StringView path_view;
    reader.read_string(path_view);
    ou::string path(path_view.ptr, path_view.len);
    auto result = handle_open(path, msg.args[0]);
    if (result.is_err()) {
      resp.error_code = result.error();
    } else {
      resp.values[0] = result.value().raw();
    }
    break;
  }
  case MethodIds::Filesystem::READ: {
    auto result = handle_read(FileHandleId(msg.args[0]), msg.args[1], msg.args[2]);
    if (result.is_err()) {
      resp.error_code = result.error();
    } else {
      resp.values[0] = result.value();
    }
    break;
  }
  case MethodIds::Filesystem::WRITE: {
    // Deserialize complex arguments from comm page
    PageAddr comm = ou_get_comm_page();
    MPackReader reader(comm.as_ptr(), OT_PAGE_SIZE);
    StringView data;
    reader.read_bin(data);  // Zero-copy binary data from comm page
    auto result = handle_write(FileHandleId(msg.args[0]), msg.args[1], data);
    if (result.is_err()) {
      resp.error_code = result.error();
    } else {
      resp.values[0] = result.value();
    }
    break;
  }
  case MethodIds::Filesystem::CLOSE: {
    auto result = handle_close(FileHandleId(msg.args[0]));
    if (result.is_err()) {
      resp.error_code = result.error();
    } else {
      // No return values
    }
    break;
  }
  case MethodIds::Filesystem::READ_ALL: {
    // Deserialize complex arguments from comm page
    PageAddr comm = ou_get_comm_page();
    MPackReader reader(comm.as_ptr(), OT_PAGE_SIZE);
    StringView path_view;
    reader.read_string(path_view);
    ou::string path(path_view.ptr, path_view.len);
    auto result = handle_read_all(path);
    if (result.is_err()) {
      resp.error_code = result.error();
    } else {
      resp.values[0] = result.value();
    }
    break;
  }
  case MethodIds::Filesystem::WRITE_ALL: {
    // Deserialize complex arguments from comm page
    PageAddr comm = ou_get_comm_page();
    MPackReader reader(comm.as_ptr(), OT_PAGE_SIZE);
    StringView path_view;
    reader.read_string(path_view);
    ou::string path(path_view.ptr, path_view.len);
    StringView data;
    reader.read_bin(data);  // Zero-copy binary data from comm page
    auto result = handle_write_all(path, data);
    if (result.is_err()) {
      resp.error_code = result.error();
    } else {
      // No return values
    }
    break;
  }
  case MethodIds::Filesystem::CREATE_DIR: {
    // Deserialize complex arguments from comm page
    PageAddr comm = ou_get_comm_page();
    MPackReader reader(comm.as_ptr(), OT_PAGE_SIZE);
    StringView path_view;
    reader.read_string(path_view);
    ou::string path(path_view.ptr, path_view.len);
    auto result = handle_create_dir(path);
    if (result.is_err()) {
      resp.error_code = result.error();
    } else {
      // No return values
    }
    break;
  }
  case MethodIds::Filesystem::DELETE_FILE: {
    // Deserialize complex arguments from comm page
    PageAddr comm = ou_get_comm_page();
    MPackReader reader(comm.as_ptr(), OT_PAGE_SIZE);
    StringView path_view;
    reader.read_string(path_view);
    ou::string path(path_view.ptr, path_view.len);
    auto result = handle_delete_file(path);
    if (result.is_err()) {
      resp.error_code = result.error();
    } else {
      // No return values
    }
    break;
  }
  case MethodIds::Filesystem::DELETE_DIR: {
    // Deserialize complex arguments from comm page
    PageAddr comm = ou_get_comm_page();
    MPackReader reader(comm.as_ptr(), OT_PAGE_SIZE);
    StringView path_view;
    reader.read_string(path_view);
    ou::string path(path_view.ptr, path_view.len);
    auto result = handle_delete_dir(path);
    if (result.is_err()) {
      resp.error_code = result.error();
    } else {
      // No return values
    }
    break;
  }
  default:
    resp.error_code = IPC__METHOD_NOT_KNOWN;
    break;
  }

  ou_ipc_reply(resp);
}

void FilesystemServer::run() {
  while (true) {
    IpcMessage msg = ou_ipc_recv();
    process_request(msg);
  }
}
