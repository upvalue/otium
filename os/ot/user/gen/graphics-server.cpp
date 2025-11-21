#include "ot/user/gen/graphics-server.hpp"
#include "ot/user/gen/method-ids.hpp"
#include "ot/user/user.hpp"

void GraphicsServerBase::process_request(const IpcMessage& msg) {
  // Check for shutdown request (handled by base class)
  if (handle_shutdown_if_requested(msg)) {
    return;  // Server exits in base class
  }

  intptr_t method = IPC_UNPACK_METHOD(msg.method_and_flags);
  uint8_t flags = IPC_UNPACK_FLAGS(msg.method_and_flags);
  IpcResponse resp = {NONE, {0, 0, 0}};

  switch (method) {
  case MethodIds::Graphics::GET_FRAMEBUFFER: {
    auto result = handle_get_framebuffer();
    if (result.is_err()) {
      resp.error_code = result.error();
    } else {
      auto val = result.value();
      resp.values[0] = val.fb_ptr;
      resp.values[1] = val.width;
      resp.values[2] = val.height;
    }
    break;
  }
  case MethodIds::Graphics::FLUSH: {
    auto result = handle_flush();
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

void GraphicsServerBase::run() {
  while (true) {
    IpcMessage msg = ou_ipc_recv();
    process_request(msg);
  }
}
