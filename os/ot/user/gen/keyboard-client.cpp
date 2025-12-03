#include "ot/user/gen/keyboard-client.hpp"
#include "ot/user/gen/method-ids.hpp"
#include "ot/user/user.hpp"

Result<PollKeyResult, ErrorCode> KeyboardClient::poll_key() {

  IpcResponse resp = ou_ipc_send(
    pid_,
    0,
    MethodIds::Keyboard::POLL_KEY,
    0, 0, 0  );

  if (resp.error_code != NONE) {
    return Result<PollKeyResult, ErrorCode>::err(resp.error_code);
  }

  PollKeyResult result;
  result.has_key = resp.values[0];
  result.code = resp.values[1];
  result.flags = resp.values[2];
  return Result<PollKeyResult, ErrorCode>::ok(result);
}


Result<bool, ErrorCode> KeyboardClient::shutdown() {
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

