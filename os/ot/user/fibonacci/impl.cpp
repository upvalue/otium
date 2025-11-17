#include "ot/user/gen/fibonacci-server.hpp"

// Simple recursive fibonacci (inefficient but demonstrates the concept)
static intptr_t calculate_fib(intptr_t n) {
  if (n <= 1) return n;
  return calculate_fib(n - 1) + calculate_fib(n - 2);
}

Result<intptr_t, ErrorCode> FibonacciServer::handle_calc_fib(intptr_t n) {
  if (n < 0 || n > 40) {  // Limit to prevent slow recursion
    return Result<intptr_t, ErrorCode>::err(FIBONACCI__INVALID_INPUT);
  }
  return Result<intptr_t, ErrorCode>::ok(calculate_fib(n));
}

Result<CalcPairResult, ErrorCode> FibonacciServer::handle_calc_pair(intptr_t n, intptr_t m) {
  if (n < 0 || n > 40 || m < 0 || m > 40) {
    return Result<CalcPairResult, ErrorCode>::err(FIBONACCI__INVALID_INPUT);
  }

  CalcPairResult result;
  result.fib_n = calculate_fib(n);
  result.fib_m = calculate_fib(m);
  return Result<CalcPairResult, ErrorCode>::ok(result);
}

Result<uintptr_t, ErrorCode> FibonacciServer::handle_get_cache_size() {
  // No cache implemented yet, return 0
  return Result<uintptr_t, ErrorCode>::ok(0);
}

void proc_fibonacci(void) {
  FibonacciServer server;
  server.run("fibonacci");
}
