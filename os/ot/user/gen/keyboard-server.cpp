#include "ot/user/gen/keyboard-server.hpp"
#include "ot/user/gen/method-ids.hpp"
#include "ot/user/user.hpp"

void KeyboardServerBase::process_request(const IpcMessage &msg) {
  // Check for shutdown request (handled by base class)
  if (handle_shutdown_if_requested(msg)) {
    return; // Server exits in base class
  }

  intptr_t method = IPC_UNPACK_METHOD(msg.method_and_flags);
  uint8_t flags = IPC_UNPACK_FLAGS(msg.method_and_flags);
  IpcResponse resp = {NONE, {0, 0, 0}};

  switch (method) {
  case MethodIds::Keyboard::POLL_KEY: {
    auto result = handle_poll_key();
    if (result.is_err()) {
      resp.error_code = result.error();
    } else {
      auto val = result.value();
      resp.values[0] = val.has_key;
      resp.values[1] = val.code;
      resp.values[2] = val.flags;
    }
    break;
  }
  default:
    resp.error_code = IPC__METHOD_NOT_KNOWN;
    break;
  }

  oprintf("keyboard server replying to method: %d with has_key, code, flags %d %d %d\n", method, resp.values[0],
          resp.values[1], resp.values[2]);

  ou_ipc_reply(resp);
}

void KeyboardServerBase::run() {
  while (true) {
    IpcMessage msg = ou_ipc_recv();
    process_request(msg);
  }
}
