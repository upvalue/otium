#pragma once
#include "ot/lib/ipc.hpp"
#include "ot/lib/result.hpp"
#include "ot/lib/error-codes.hpp"
#include "ot/user/gen/fibonacci-types.hpp"

struct FibonacciClient {
  int pid_;

  FibonacciClient(int pid) : pid_(pid) {}

  Result<intptr_t, ErrorCode> calc_fib(intptr_t n);
  Result<CalcPairResult, ErrorCode> calc_pair(intptr_t n, intptr_t m);
  Result<uintptr_t, ErrorCode> get_cache_size();

  // Universal shutdown method (sends IPC_METHOD_SHUTDOWN)
  Result<bool, ErrorCode> shutdown();
};
