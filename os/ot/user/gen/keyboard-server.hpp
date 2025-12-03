#pragma once
#include "ot/lib/ipc.hpp"
#include "ot/lib/result.hpp"
#include "ot/lib/error-codes.hpp"
#include "ot/lib/typed-int.hpp"
#include "ot/lib/string-view.hpp"
#include "ot/user/gen/keyboard-types.hpp"
#include "ot/user/gen/server-base.hpp"

struct KeyboardServerBase : ServerBase {
  // Virtual destructor for proper cleanup
  virtual ~KeyboardServerBase() = default;

  // Pure virtual methods to implement in derived class
  virtual Result<PollKeyResult, ErrorCode> handle_poll_key() = 0;

  // Framework methods - handles dispatch
  void process_request(const IpcMessage& msg);
  void run();
};
