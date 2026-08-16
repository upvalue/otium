// Monotonic benchmark timing and wall-clock time.
#include "../builtins.hpp"
#include <chrono>

namespace ot {

static Value nat_current_jiffy(State& vm, u32, u32 argc) {
  OT_TRY(need_argc(vm, "current-jiffy", argc, 0, 0));
  using Clock = std::chrono::steady_clock;
  i64 ns =
      std::chrono::duration_cast<std::chrono::nanoseconds>(Clock::now().time_since_epoch()).count();
  return int_v(ns);
}

static Value nat_jiffies_per_second(State& vm, u32, u32 argc) {
  OT_TRY(need_argc(vm, "jiffies-per-second", argc, 0, 0));
  return int_v(1000000000);
}

static Value nat_current_second(State& vm, u32, u32 argc) {
  OT_TRY(need_argc(vm, "current-second", argc, 0, 0));
  using Clock = std::chrono::system_clock;
  std::chrono::duration<f64> elapsed = Clock::now().time_since_epoch();
  return float_v(elapsed.count());
}

void register_time(State& vm) {
  def_native(vm, "current-jiffy", nat_current_jiffy);
  def_native(vm, "jiffies-per-second", nat_jiffies_per_second);
  def_native(vm, "current-second", nat_current_second);
}

}  // namespace ot
