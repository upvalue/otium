#include "ot/user/gen/graphics-client.hpp"
#include "ot/user/gen/method-ids.hpp"
#include "ot/user/user.hpp"
#include "ot/lib/mpack/mpack-reader.hpp"

Result<GetFramebufferResult, ErrorCode> GraphicsClient::get_framebuffer() {

  IpcResponse resp = ou_ipc_send(
    pid_,
    0,
    MethodIds::Graphics::GET_FRAMEBUFFER,
    0, 0, 0  );

  if (resp.error_code != NONE) {
    return Result<GetFramebufferResult, ErrorCode>::err(resp.error_code);
  }

  GetFramebufferResult result;
  result.fb_ptr = resp.values[0];
  result.width = resp.values[1];
  result.height = resp.values[2];
  return Result<GetFramebufferResult, ErrorCode>::ok(result);
}

Result<bool, ErrorCode> GraphicsClient::flush() {

  IpcResponse resp = ou_ipc_send(
    pid_,
    0,
    MethodIds::Graphics::FLUSH,
    0, 0, 0  );

  if (resp.error_code != NONE) {
    return Result<bool, ErrorCode>::err(resp.error_code);
  }

  return Result<bool, ErrorCode>::ok({});
}

Result<uintptr_t, ErrorCode> GraphicsClient::register_app(const char* name) {
  // Serialize complex arguments to comm page
  CommWriter writer;
  writer.writer().str(name);

  IpcResponse resp = ou_ipc_send(
    pid_,
    IPC_FLAG_SEND_COMM_DATA,
    MethodIds::Graphics::REGISTER_APP,
    0, 0, 0  );

  if (resp.error_code != NONE) {
    return Result<uintptr_t, ErrorCode>::err(resp.error_code);
  }

  return Result<uintptr_t, ErrorCode>::ok(resp.values[0]);
}

Result<uintptr_t, ErrorCode> GraphicsClient::should_render() {

  IpcResponse resp = ou_ipc_send(
    pid_,
    0,
    MethodIds::Graphics::SHOULD_RENDER,
    0, 0, 0  );

  if (resp.error_code != NONE) {
    return Result<uintptr_t, ErrorCode>::err(resp.error_code);
  }

  return Result<uintptr_t, ErrorCode>::ok(resp.values[0]);
}


Result<bool, ErrorCode> GraphicsClient::shutdown() {
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

