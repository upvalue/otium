#include "ot/user/gen/fibonacci-server.hpp"
#include "ot/user/gen/method-ids.hpp"
#include "ot/user/user.hpp"

void FibonacciServer::process_request(const IpcMessage& msg) {
  // Check for shutdown request (handled by base class)
  if (handle_shutdown_if_requested(msg)) {
    return;  // Server exits in base class
  }

  intptr_t method = IPC_UNPACK_METHOD(msg.method_and_flags);
  IpcResponse resp = {NONE, {0, 0, 0}};

  switch (method) {
  case MethodIds::Fibonacci::CALC_FIB: {
    auto result = handle_calc_fib(msg.args[0]);
    if (result.is_err()) {
      resp.error_code = result.error();
    } else {
      resp.values[0] = result.value();
    }
    break;
  }
  case MethodIds::Fibonacci::CALC_PAIR: {
    auto result = handle_calc_pair(msg.args[0], msg.args[1]);
    if (result.is_err()) {
      resp.error_code = result.error();
    } else {
      auto val = result.value();
      resp.values[0] = val.fib_n;
      resp.values[1] = val.fib_m;
    }
    break;
  }
  case MethodIds::Fibonacci::GET_CACHE_SIZE: {
    auto result = handle_get_cache_size();
    if (result.is_err()) {
      resp.error_code = result.error();
    } else {
      resp.values[0] = result.value();
    }
    break;
  }
  default:
    resp.error_code = IPC__METHOD_NOT_KNOWN;
    break;
  }

  ou_ipc_reply(resp);
}

void FibonacciServer::run() {
  while (true) {
    IpcMessage msg = ou_ipc_recv();
    process_request(msg);
  }
}
