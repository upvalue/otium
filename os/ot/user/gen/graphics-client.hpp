#pragma once
#include "ot/lib/ipc.hpp"
#include "ot/lib/result.hpp"
#include "ot/lib/error-codes.hpp"
#include "ot/lib/typed-int.hpp"
#include "ot/user/gen/graphics-types.hpp"

struct GraphicsClient {
  Pid pid_;

  GraphicsClient(Pid pid) : pid_(pid) {}

  Result<GetFramebufferResult, ErrorCode> get_framebuffer();
  Result<bool, ErrorCode> flush();

  // Universal shutdown method (sends IPC_METHOD_SHUTDOWN)
  Result<bool, ErrorCode> shutdown();
};
