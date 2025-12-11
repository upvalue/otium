#pragma once
#include "ot/lib/ipc.hpp"
#include "ot/lib/result.hpp"
#include "ot/lib/error-codes.hpp"
#include "ot/lib/typed-int.hpp"
#include "ot/lib/string-view.hpp"
#include "ot/user/gen/graphics-types.hpp"
#include "ot/user/gen/server-base.hpp"
#include "ot/user/string.hpp"

struct GraphicsServerBase : ServerBase {
  // Virtual destructor for proper cleanup
  virtual ~GraphicsServerBase() = default;

  // Pure virtual methods to implement in derived class
  virtual Result<GetFramebufferResult, ErrorCode> handle_get_framebuffer() = 0;
  virtual Result<bool, ErrorCode> handle_flush() = 0;
  virtual Result<uintptr_t, ErrorCode> handle_register_app(const StringView& name) = 0;
  virtual Result<uintptr_t, ErrorCode> handle_should_render() = 0;
  virtual Result<bool, ErrorCode> handle_unregister_app() = 0;

  // Framework methods - handles dispatch
  void process_request(const IpcMessage& msg);
  void run();
};
