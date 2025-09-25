#include "pico.c"

__attribute__((naked)) void start() {
  /**
   * Reset peripherals we're going to use; the first register we use is the
   * FRCE_ON register. (Since it has an offset of 0x0, there's no additional
   * definition for it).
   */
  REG(RESETS_BASE + REG_ALIAS_CLR_BITS) =
      RESET_IO_BANK0_BITS | RESET_PADS_BANK0_BITS;

  // Then RESET_DONE
  while ((~REG(RESETS_BASE + RESETS_DONE_OFFSET)) &
         (RESET_IO_BANK0_BITS | RESET_PADS_BANK0_BITS))
    ;

  /*
   * Here we're saying that we're going to use PIN 7 for SIO (single purpose
   * input/output) by selecting function 5
   */
  REG(IO_BANK0_BASE + IO_BANK0_GPIO7_CTRL_OFFSET) = GPIO_FUNC_SIO;

  REG(PADS_BANK0_BASE + PADS_BANK0_GPIO7_OFFSET + REG_ALIAS_CLR_BITS) =
      PADS_BANK0_GPIO0_ISO_BITS;
  REG(SIO_BASE + SIO_GPIO_OE_SET_OFFSET) = 1 << 7;

  /**
   * Then we XOR GPIO pin 7 to toggle it on and off
   */
  for (;;) {
    REG(SIO_BASE + SIO_GPIO_OUT_XOR_OFFSET) = 1 << 7;
    for (unsigned int i = 0; i != 1000000; i++)
      ;
  }
}
