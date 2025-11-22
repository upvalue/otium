#include "ot/user/gen/fibonacci-server.hpp"
#include "ot/user/local-storage.hpp"

// Storage for Fibonacci server
struct FibonacciStorage : public LocalStorage {
  FibonacciStorage() {
    process_storage_init(5); // 20KB for fibonacci sequences
  }
};

// Fibonacci server implementation
struct FibonacciServer : FibonacciServerBase {
private:
  // Simple recursive fibonacci (inefficient but demonstrates the concept)
  static intptr_t calculate_fib(intptr_t n) {
    if (n <= 1)
      return n;
    return calculate_fib(n - 1) + calculate_fib(n - 2);
  }

public:
  Result<intptr_t, ErrorCode> handle_calc_fib(intptr_t n) override {
    if (n < 0 || n > 40) { // Limit to prevent slow recursion
      return Result<intptr_t, ErrorCode>::err(FIBONACCI__INVALID_INPUT);
    }
    return Result<intptr_t, ErrorCode>::ok(calculate_fib(n));
  }

  Result<CalcPairResult, ErrorCode> handle_calc_pair(intptr_t n, intptr_t m) override {
    if (n < 0 || n > 40 || m < 0 || m > 40) {
      return Result<CalcPairResult, ErrorCode>::err(FIBONACCI__INVALID_INPUT);
    }

    CalcPairResult result;
    result.fib_n = calculate_fib(n);
    result.fib_m = calculate_fib(m);
    return Result<CalcPairResult, ErrorCode>::ok(result);
  }

  Result<uintptr_t, ErrorCode> handle_get_cache_size() override {
    // No cache implemented yet, return 0
    return Result<uintptr_t, ErrorCode>::ok(0);
  }

  Result<FibResult, ErrorCode> handle_calc_fib_detailed(uintptr_t n) override {
    if (n > 40) {
      return Result<FibResult, ErrorCode>::err(FIBONACCI__INVALID_INPUT);
    }

    FibResult result;
    result.value = calculate_fib(n);
    result.is_cached = false; // No caching yet
    return Result<FibResult, ErrorCode>::ok(static_cast<FibResult&&>(result));
  }

  Result<ou::vector<uintptr_t>, ErrorCode> handle_calc_sequence(uintptr_t start, uintptr_t count) override {
    if (start > 40 || count > 20 || start + count > 40) {
      return Result<ou::vector<uintptr_t>, ErrorCode>::err(FIBONACCI__INVALID_INPUT);
    }

    ou::vector<uintptr_t> result;
    result.reserve(count);
    for (uintptr_t i = 0; i < count; i++) {
      result.push_back(calculate_fib(start + i));
    }
    return Result<ou::vector<uintptr_t>, ErrorCode>::ok(static_cast<ou::vector<uintptr_t>&&>(result));
  }
};

void proc_fibonacci(void) {
  // Initialize storage for vector allocations
  process_storage_init(5); // 20KB should be enough for small fibonacci sequences

  FibonacciServer server;
  server.run();
}
