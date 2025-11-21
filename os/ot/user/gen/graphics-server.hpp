#pragma once
#include "ot/lib/ipc.hpp"
#include "ot/lib/result.hpp"
#include "ot/lib/error-codes.hpp"
#include "ot/lib/typed-int.hpp"
#include "ot/lib/string-view.hpp"
#include "ot/user/gen/graphics-types.hpp"
#include "ot/user/gen/server-base.hpp"

struct GraphicsServer : ServerBase {
  // Methods to implement
  Result<GetFramebufferResult, ErrorCode> handle_get_framebuffer();
  Result<bool, ErrorCode> handle_flush();

  // Framework methods - handles dispatch
  void process_request(const IpcMessage& msg);
  void run();
};
