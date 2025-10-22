#include "ot/common.h"

uint64_t o_time_get(void) {
  uint64_t time;
  asm volatile("rdtime %0" : "=r"(time));
  return time;
}