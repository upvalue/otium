#pragma once
#include "ot/lib/ipc.hpp"
#include "ot/lib/result.hpp"
#include "ot/lib/error-codes.hpp"
#include "ot/lib/typed-int.hpp"
#include "ot/lib/string-view.hpp"
#include "ot/user/gen/fibonacci-types.hpp"
#include "ot/user/gen/server-base.hpp"

struct FibonacciServer : ServerBase {
  // Methods to implement
  Result<intptr_t, ErrorCode> handle_calc_fib(intptr_t n);
  Result<CalcPairResult, ErrorCode> handle_calc_pair(intptr_t n, intptr_t m);
  Result<uintptr_t, ErrorCode> handle_get_cache_size();

  // Framework methods - handles dispatch
  void process_request(const IpcMessage& msg);
  void run();
};
