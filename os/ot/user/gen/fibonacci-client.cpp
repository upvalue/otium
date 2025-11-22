#include "ot/user/gen/fibonacci-client.hpp"
#include "ot/user/gen/method-ids.hpp"
#include "ot/user/user.hpp"
#include "ot/lib/mpack/mpack-reader.hpp"

Result<intptr_t, ErrorCode> FibonacciClient::calc_fib(intptr_t n) {

  IpcResponse resp = ou_ipc_send(
    pid_,
    0,
    MethodIds::Fibonacci::CALC_FIB,
    n, 0, 0  );

  if (resp.error_code != NONE) {
    return Result<intptr_t, ErrorCode>::err(resp.error_code);
  }

  return Result<intptr_t, ErrorCode>::ok(resp.values[0]);
}

Result<CalcPairResult, ErrorCode> FibonacciClient::calc_pair(intptr_t n, intptr_t m) {

  IpcResponse resp = ou_ipc_send(
    pid_,
    0,
    MethodIds::Fibonacci::CALC_PAIR,
    n, m, 0  );

  if (resp.error_code != NONE) {
    return Result<CalcPairResult, ErrorCode>::err(resp.error_code);
  }

  CalcPairResult result;
  result.fib_n = resp.values[0];
  result.fib_m = resp.values[1];
  return Result<CalcPairResult, ErrorCode>::ok(result);
}

Result<uintptr_t, ErrorCode> FibonacciClient::get_cache_size() {

  IpcResponse resp = ou_ipc_send(
    pid_,
    0,
    MethodIds::Fibonacci::GET_CACHE_SIZE,
    0, 0, 0  );

  if (resp.error_code != NONE) {
    return Result<uintptr_t, ErrorCode>::err(resp.error_code);
  }

  return Result<uintptr_t, ErrorCode>::ok(resp.values[0]);
}

Result<FibResult, ErrorCode> FibonacciClient::calc_fib_detailed(uintptr_t n) {

  IpcResponse resp = ou_ipc_send(
    pid_,
    0 | IPC_FLAG_RECV_COMM_DATA,
    MethodIds::Fibonacci::CALC_FIB_DETAILED,
    n, 0, 0  );

  if (resp.error_code != NONE) {
    return Result<FibResult, ErrorCode>::err(resp.error_code);
  }

  // Response data is in comm page - caller reads it with MPackReader
  // Return value indicates size or count
  // Deserialize message type from comm page
  PageAddr comm = ou_get_comm_page();
  MPackReader reader(comm.as_ptr(), OT_PAGE_SIZE);
  auto result = FibResult::unpack(reader);
  if (result.is_err()) {
    return Result<FibResult, ErrorCode>::err(result.error());
  }
  return Result<FibResult, ErrorCode>::ok(static_cast<FibResult&&>(result.value()));
}

Result<ou::vector<uintptr_t>, ErrorCode> FibonacciClient::calc_sequence(uintptr_t start, uintptr_t count) {

  IpcResponse resp = ou_ipc_send(
    pid_,
    0 | IPC_FLAG_RECV_COMM_DATA,
    MethodIds::Fibonacci::CALC_SEQUENCE,
    start, count, 0  );

  if (resp.error_code != NONE) {
    return Result<ou::vector<uintptr_t>, ErrorCode>::err(resp.error_code);
  }

  // Response data is in comm page - caller reads it with MPackReader
  // Return value indicates size or count
  // Deserialize message type from comm page
  PageAddr comm = ou_get_comm_page();
  MPackReader reader(comm.as_ptr(), OT_PAGE_SIZE);
  auto result = FibArrayHelper::unpack(reader);
  if (result.is_err()) {
    return Result<ou::vector<uintptr_t>, ErrorCode>::err(result.error());
  }
  return Result<ou::vector<uintptr_t>, ErrorCode>::ok(static_cast<ou::vector<uintptr_t>&&>(result.value()));
}


Result<bool, ErrorCode> FibonacciClient::shutdown() {
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

