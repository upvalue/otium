#pragma once
#include "ot/lib/ipc.hpp"
#include "ot/lib/result.hpp"
#include "ot/lib/error-codes.hpp"
#include "ot/lib/typed-int.hpp"
#include "ot/user/gen/keyboard-types.hpp"

struct KeyboardClient {
  Pid pid_;

  KeyboardClient(Pid pid) : pid_(pid) {}

  Result<PollKeyResult, ErrorCode> poll_key();

  // Universal shutdown method (sends IPC_METHOD_SHUTDOWN)
  Result<bool, ErrorCode> shutdown();
};
