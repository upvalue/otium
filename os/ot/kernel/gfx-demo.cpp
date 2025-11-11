#include <stdint.h>

#include "ot/kernel/kernel.hpp"

void graphics_demo_main_proc() {
  oprintf("terminating proc pid %d\n", current_proc->pid);
  current_proc->state = TERMINATED;
  while (true) {
    yield();
  }
}