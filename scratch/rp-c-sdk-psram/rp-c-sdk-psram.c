#include "hardware/clocks.h"
#include "hardware/exception.h"
#include "hardware/structs/qmi.h"
#include "hardware/structs/xip_ctrl.h"
#include "hardware/sync.h"
#include "pico/runtime_init.h"
#include "pico/stdlib.h"
#include <stdio.h>

extern uint8_t __psram_start;

#define RP2350_PSRAM_MAX_SCK_HZ (109 * 1000 * 1000)
#define PSRAM_GPIO_PIN 8

#define PSRAM __attribute__((section(".psram")))

void hard_fault_handler(void) {
  printf("aw shucks\n");
  while (1) {
    sleep_ms(1000);
  }
}

size_t __no_inline_not_in_flash_func(psram_detect)(void) {
  // printf("psram_detect entered\n");

  // printf("Stack pointer: %p\n", (void *)__get_MSP());
  int psram_size = 0;

  // Set pin

  // gpio pin 8 setup necessary?
  // gpio_set_function(PSRAM_GPIO_PIN, GPIO_FUNC_XIP_CS1);

  // printf("save_and_disable_interrupts\n");
  // uint32_t intr_stash = save_and_disable_interrupts();

  // Try and read the PSRAM ID via direct_csr.
  // printf("debug1\n");
  qmi_hw->direct_csr = 30 << QMI_DIRECT_CSR_CLKDIV_LSB | QMI_DIRECT_CSR_EN_BITS;

  // Need to poll for the cooldown on the last XIP transfer to expire
  // (via direct-mode BUSY flag) before it is safe to perform the first
  // direct-mode operation
  // printf("debug2: polling for cooldown\n");
  while ((qmi_hw->direct_csr & QMI_DIRECT_CSR_BUSY_BITS) != 0) {
  }

  // printf("debug3: exit out of qmi\n");
  // Exit out of QMI in case we've inited already
  qmi_hw->direct_csr |= QMI_DIRECT_CSR_ASSERT_CS1N_BITS;

  // printf("debug4: transmit as quad\n");
  // Transmit as quad.
  qmi_hw->direct_tx = QMI_DIRECT_TX_OE_BITS |
                      QMI_DIRECT_TX_IWIDTH_VALUE_Q << QMI_DIRECT_TX_IWIDTH_LSB |
                      0xf5;

  while ((qmi_hw->direct_csr & QMI_DIRECT_CSR_BUSY_BITS) != 0) {
  }

  (void)qmi_hw->direct_rx;

  qmi_hw->direct_csr &= ~(QMI_DIRECT_CSR_ASSERT_CS1N_BITS);

  // Read the id
  qmi_hw->direct_csr |= QMI_DIRECT_CSR_ASSERT_CS1N_BITS;
  uint8_t kgd = 0;
  uint8_t eid = 0;

  for (size_t i = 0; i < 7; i++) {
    if (i == 0) {
      qmi_hw->direct_tx = 0x9f;
    } else {
      qmi_hw->direct_tx = 0xff;
    }

    while ((qmi_hw->direct_csr & QMI_DIRECT_CSR_TXEMPTY_BITS) == 0) {
    }

    while ((qmi_hw->direct_csr & QMI_DIRECT_CSR_BUSY_BITS) != 0) {
    }

    if (i == 5) {
      kgd = qmi_hw->direct_rx;
    } else if (i == 6) {
      eid = qmi_hw->direct_rx;
    } else {
      (void)qmi_hw->direct_rx;
    }
  }

  // Disable direct csr.
  qmi_hw->direct_csr &=
      ~(QMI_DIRECT_CSR_ASSERT_CS1N_BITS | QMI_DIRECT_CSR_EN_BITS);

  // while (qmi_hw->direct_csr & QMI_DIRECT_CSR_BUSY_BITS) {}

  if (kgd == 0x5D) {
    psram_size = 1024 * 1024; // 1 MiB
    uint8_t size_id = eid >> 5;
    if (eid == 0x26 || size_id == 2) {
      psram_size *= 8; // 8 MiB
    } else if (size_id == 0) {
      psram_size *= 2; // 2 MiB
    } else if (size_id == 1) {
      psram_size *= 4; // 4 MiB
    }
  }
  // restore_interrupts(intr_stash);

  return psram_size;
}

size_t __no_inline_not_in_flash_func(psram_init)(uint cs_pin) {
  gpio_set_function(cs_pin, GPIO_FUNC_XIP_CS1);

  uint32_t intr_stash = save_and_disable_interrupts();

  size_t psram_size = psram_detect();

  if (!psram_size) {
    restore_interrupts(intr_stash);
    return 0;
  }

  // RN: Becomes unresponsive here (seemingly)

  // Enable direct mode, PSRAM CS, clkdiv of 10.
  qmi_hw->direct_csr = 10 << QMI_DIRECT_CSR_CLKDIV_LSB |
                       QMI_DIRECT_CSR_EN_BITS | QMI_DIRECT_CSR_AUTO_CS1N_BITS;
  // restore_interrupts(intr_stash);
  // return 0;

  while (qmi_hw->direct_csr & QMI_DIRECT_CSR_BUSY_BITS) {
  }

  // Enable QPI mode on the PSRAM
  const uint CMD_QPI_EN = 0x35;
  qmi_hw->direct_tx = QMI_DIRECT_TX_NOPUSH_BITS | CMD_QPI_EN;

  while (qmi_hw->direct_csr & QMI_DIRECT_CSR_BUSY_BITS) {
  }

  // Set PSRAM timing for APS6404
  //
  // Using an rxdelay equal to the divisor isn't enough when running the APS6404
  // close to 133MHz. So: don't allow running at divisor 1 above 100MHz (because
  // delay of 2 would be too late), and add an extra 1 to the rxdelay if the
  // divided clock is > 100MHz (i.e. sys clock > 200MHz).
  const int max_psram_freq = RP2350_PSRAM_MAX_SCK_HZ;
  const int clock_hz = clock_get_hz(clk_sys);
  int divisor = (clock_hz + max_psram_freq - 1) / max_psram_freq;
  if (divisor == 1 && clock_hz > 100000000) {
    divisor = 2;
  }
  int rxdelay = divisor;
  if (clock_hz / divisor > 100000000) {
    rxdelay += 1;
  }

  // - Max select must be <= 8us.  The value is given in multiples of 64 system
  // clocks.
  // - Min deselect must be >= 18ns.  The value is given in system clock cycles
  // - ceil(divisor / 2).
  const int clock_period_fs = 1000000000000000ll / clock_hz;
  const int max_select = (125 * 1000000) / clock_period_fs; // 125 = 8000ns / 64
  const int min_deselect =
      (18 * 1000000 + (clock_period_fs - 1)) / clock_period_fs -
      (divisor + 1) / 2;

  qmi_hw->m[1].timing = 1 << QMI_M1_TIMING_COOLDOWN_LSB |
                        QMI_M1_TIMING_PAGEBREAK_VALUE_1024
                            << QMI_M1_TIMING_PAGEBREAK_LSB |
                        max_select << QMI_M1_TIMING_MAX_SELECT_LSB |
                        min_deselect << QMI_M1_TIMING_MIN_DESELECT_LSB |
                        rxdelay << QMI_M1_TIMING_RXDELAY_LSB |
                        divisor << QMI_M1_TIMING_CLKDIV_LSB;

  // Set PSRAM commands and formats
  qmi_hw->m[1].rfmt =
      QMI_M0_RFMT_PREFIX_WIDTH_VALUE_Q << QMI_M0_RFMT_PREFIX_WIDTH_LSB |
      QMI_M0_RFMT_ADDR_WIDTH_VALUE_Q << QMI_M0_RFMT_ADDR_WIDTH_LSB |
      QMI_M0_RFMT_SUFFIX_WIDTH_VALUE_Q << QMI_M0_RFMT_SUFFIX_WIDTH_LSB |
      QMI_M0_RFMT_DUMMY_WIDTH_VALUE_Q << QMI_M0_RFMT_DUMMY_WIDTH_LSB |
      QMI_M0_RFMT_DATA_WIDTH_VALUE_Q << QMI_M0_RFMT_DATA_WIDTH_LSB |
      QMI_M0_RFMT_PREFIX_LEN_VALUE_8 << QMI_M0_RFMT_PREFIX_LEN_LSB |
      6 << QMI_M0_RFMT_DUMMY_LEN_LSB;

  qmi_hw->m[1].rcmd = 0xEB;

  qmi_hw->m[1].wfmt =
      QMI_M0_WFMT_PREFIX_WIDTH_VALUE_Q << QMI_M0_WFMT_PREFIX_WIDTH_LSB |
      QMI_M0_WFMT_ADDR_WIDTH_VALUE_Q << QMI_M0_WFMT_ADDR_WIDTH_LSB |
      QMI_M0_WFMT_SUFFIX_WIDTH_VALUE_Q << QMI_M0_WFMT_SUFFIX_WIDTH_LSB |
      QMI_M0_WFMT_DUMMY_WIDTH_VALUE_Q << QMI_M0_WFMT_DUMMY_WIDTH_LSB |
      QMI_M0_WFMT_DATA_WIDTH_VALUE_Q << QMI_M0_WFMT_DATA_WIDTH_LSB |
      QMI_M0_WFMT_PREFIX_LEN_VALUE_8 << QMI_M0_WFMT_PREFIX_LEN_LSB;

  qmi_hw->m[1].wcmd = 0x38;

  // Disable direct mode
  qmi_hw->direct_csr = 0;

  // Enable writes to PSRAM
  hw_set_bits(&xip_ctrl_hw->ctrl, XIP_CTRL_WRITABLE_M1_BITS);

  restore_interrupts(intr_stash);

  return psram_size;
}
static void __no_inline_not_in_flash_func(psram_init_runtime)() {
  psram_init(PSRAM_GPIO_PIN);
}

// PICO_RUNTIME_INIT_FUNC_RUNTIME(psram_init_runtime, "11001");

static int mycoolmemory[8] PSRAM;

static unsigned char mycoolmemory2[6 * 1024 * 1024] PSRAM;

#define COOLMEMOFSSET 5 * 1024 * 1024

int main() {
  /**
   * Initialize standard I/O with the SDK
   */
  stdio_init_all();
  // psram_init_runtime();

  /** Wait for a little bit to make sure everything's ready */
  sleep_ms(1000);

  /** Set up an exception handler */
  exception_set_exclusive_handler(HARDFAULT_EXCEPTION, hard_fault_handler);

  /** Wait for user input before running the actual program so we can capture
   * logs */
  while (true) {
    printf("waiting for user input to begin\n");
    int c = getchar();
    if (c != EOF) {
      printf("program r1\n");
      printf("detected input, beginning program\n");
    }
    break;
  }
  size_t psram_size = psram_init(PSRAM_GPIO_PIN);
  printf("got some psram of size %ld\n", psram_size);

  printf("qmi_hw address: %xd\n", (size_t)qmi_hw);
  sleep_ms(500);
  printf("psram_detect address: %p\n", (void *)psram_detect);
  sleep_ms(500);

  // size_t psram_size = psram_detect();

  // printf("detected psram of size %ld\n", psram_size);

  // psram_init(PSRAM_GPIO_PIN);

  printf("psram ready for use\n");
  printf("psram start: %p\n", &__psram_start);

  for (int i = 0; i != 8; i++) {
    mycoolmemory[i] = i;
  }

  mycoolmemory2[COOLMEMOFSSET] = 10;
  while (true) {
    printf("mycoolmemory location: %p\n", (void *)mycoolmemory);
    for (int i = 0; i != 8; i++) {
      printf("mycoolmemory %d=%d\n", i, mycoolmemory[i]);
    }
    printf("mycoolmemory2 location: %p\n",
           (void *)&mycoolmemory2[COOLMEMOFSSET]);
    printf("mycoolmemory2 %d=%d\n", 5 * 1024, mycoolmemory2[COOLMEMOFSSET]);

    sleep_ms(1000);
  }
}
