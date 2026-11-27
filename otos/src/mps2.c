/* Board layer for QEMU's mps2-an505, a Cortex-M33: the vector table and reset
 * code, a CMSDK UART console, SysTick for the clock, fault reports, and reset
 * for shutdown. The CPU stays in the secure state, so peripherals are used
 * through their secure aliases. */
#include "kernel.h"

#include <stdio.h>
#include <string.h>

/* SysTick runs from the 20 MHz CPU clock and interrupts once a millisecond. */
#define CPU_HZ 20000000u
#define TICK_RELOAD (CPU_HZ / 1000u)
#define NS_PER_CYCLE (1000000000u / CPU_HZ)

#define REG(address) (*(volatile uint32_t*)(address))

/* System control space. */
#define SYST_CSR REG(0xe000e010)
#define SYST_RVR REG(0xe000e014)
#define SYST_CVR REG(0xe000e018)
#define NVIC_ISER(n) REG(0xe000e100 + 4 * (n))
#define SCB_AIRCR REG(0xe000ed0c)
#define SCB_SHCSR REG(0xe000ed24)
#define SCB_CFSR REG(0xe000ed28)
#define SCB_HFSR REG(0xe000ed2c)
#define SCB_MMFAR REG(0xe000ed34)
#define SCB_BFAR REG(0xe000ed38)
#define SCB_CPACR REG(0xe000ed88)
#define SAU_SFSR REG(0xe000ede4)
#define SAU_SFAR REG(0xe000ede8)

/* UART0, a CMSDK APB UART on the 20 MHz peripheral clock. */
#define UART_BASE 0x50200000u
#define UART_DATA REG(UART_BASE + 0x00)
#define UART_STATE REG(UART_BASE + 0x04)
#define UART_CTRL REG(UART_BASE + 0x08)
#define UART_INTCLEAR REG(UART_BASE + 0x0c)
#define UART_BAUDDIV REG(UART_BASE + 0x10)
#define UART_STATE_TX_FULL 0x1u
#define UART_STATE_RX_FULL 0x2u
#define UART_CTRL_TX 0x1u
#define UART_CTRL_RX 0x2u
#define UART_CTRL_RX_INTERRUPT 0x8u
#define UART_INTERRUPT_RX 0x2u
#define UART0_RX_IRQ 32
#define BAUD 115200u

#define IRQ_COUNT 64

static void uart_put(char c) {
  while (UART_STATE & UART_STATE_TX_FULL) {}
  UART_DATA = (uint8_t)c;
}

void console_write(const char* bytes, size_t length) {
  for (size_t i = 0; i < length; i++) {
    if (bytes[i] == '\n') uart_put('\r');
    uart_put(bytes[i]);
  }
}

/* Input bytes, filled by the UART interrupt and emptied by console_read_byte. */
static unsigned char input[256];
static volatile size_t input_head, input_tail;

volatile sig_atomic_t console_interrupt;
volatile bool console_evaluating;

/* While Otium code runs, ^C sets the interrupt flag and drops typeahead.
 * Otherwise it reaches the line editor like any other byte. */
static void uart0_rx_handler(void) {
  while (UART_STATE & UART_STATE_RX_FULL) {
    unsigned char c = (unsigned char)UART_DATA;
    if (c == 0x03 && console_evaluating) {
      console_interrupt = 1;
      input_tail = input_head;
      continue;
    }
    size_t next = (input_head + 1) % sizeof input;
    if (next == input_tail) continue;
    input[input_head] = c;
    input_head = next;
  }
  UART_INTCLEAR = UART_INTERRUPT_RX;
}

int console_read_byte(void) {
  for (;;) {
    /* With interrupts masked, a byte can't arrive between the check and wfi;
     * wfi still wakes for the pending interrupt. */
    __asm__ volatile("cpsid i" ::: "memory");
    bool empty = input_tail == input_head;
    bool interrupted = console_evaluating && console_interrupt;
    if (empty && !interrupted) __asm__ volatile("wfi");
    __asm__ volatile("cpsie i" ::: "memory");
    if (interrupted) return -1;
    if (!empty) break;
  }
  unsigned char c = input[input_tail];
  input_tail = (input_tail + 1) % sizeof input;
  return c;
}

static volatile uint64_t ticks;

static void systick_handler(void) { ticks++; }

uint64_t kernel_uptime_ns(void) {
  uint64_t ms;
  uint32_t left;
  /* Read again if the tick interrupt came in between. */
  do {
    ms = ticks;
    left = SYST_CVR;
  } while (ms != ticks);
  return ms * 1000000u + (uint64_t)(TICK_RELOAD - 1 - left) * NS_PER_CYCLE;
}

[[noreturn]] void board_reset(bool failure) {
  /* QEMU's -no-reboot turns the reset request into an exit. */
  (void)failure;
  __asm__ volatile("dsb" ::: "memory");
  SCB_AIRCR = 0x05fa0004u; /* VECTKEY, SYSRESETREQ */
  __asm__ volatile("dsb" ::: "memory");
  for (;;) __asm__ volatile("wfi");
}

/* picolibc's exit and abort end here. */
[[noreturn]] void _exit(int status) { board_reset(status != 0); }

[[noreturn]] void fault_report(const uint32_t* frame, uint32_t exc_return) {
  uint32_t ipsr;
  __asm__ volatile("mrs %0, ipsr" : "=r"(ipsr));
  /* A stack overflow stops before the frame is fully stacked, so its pc and
   * lr are not to be trusted. */
  if (SCB_CFSR & (1u << 20)) puts("otos: stack overflow");
  printf("otos: fault %lu pc=%#lx lr=%#lx cfsr=%#lx hfsr=%#lx mmfar=%#lx bfar=%#lx sfsr=%#lx "
         "sfar=%#lx exc_return=%#lx\n",
         (unsigned long)(ipsr & 0x1ff), (unsigned long)frame[6], (unsigned long)frame[5],
         (unsigned long)SCB_CFSR, (unsigned long)SCB_HFSR, (unsigned long)SCB_MMFAR,
         (unsigned long)SCB_BFAR, (unsigned long)SAU_SFSR, (unsigned long)SAU_SFAR,
         (unsigned long)exc_return);
  board_reset(true);
}

/* Passes the stacked frame to fault_report, then moves to the top of the
 * stack, in case the fault was an overflow. */
[[gnu::naked]] static void fault_handler(void) {
  __asm__ volatile(
      "tst lr, #4\n"
      "ite eq\n"
      "mrseq r0, msp\n"
      "mrsne r0, psp\n"
      "mov r1, lr\n"
      "ldr r2, =__stack_top\n"
      "msr msp, r2\n"
      "b fault_report\n");
}

static void unexpected_handler(void) {
  uint32_t ipsr;
  __asm__ volatile("mrs %0, ipsr" : "=r"(ipsr));
  printf("otos: unexpected exception %lu\n", (unsigned long)(ipsr & 0x1ff));
  board_reset(true);
}

static void board_init(void) {
  UART_BAUDDIV = CPU_HZ / BAUD;
  UART_CTRL = UART_CTRL_TX | UART_CTRL_RX | UART_CTRL_RX_INTERRUPT;
  NVIC_ISER(UART0_RX_IRQ / 32) = 1u << (UART0_RX_IRQ % 32);

  SYST_RVR = TICK_RELOAD - 1;
  SYST_CVR = 0;
  SYST_CSR = 0x7; /* CPU clock, interrupt, enable */
}

extern uint32_t __data_start[], __data_end[], __data_load[], __bss_start[], __bss_end[];
extern uint32_t __stack_limit[], __stack_top[];

[[noreturn]] void reset_handler(void) {
  memcpy(__data_start, __data_load, (size_t)((char*)__data_end - (char*)__data_start));
  memset(__bss_start, 0, (size_t)((char*)__bss_end - (char*)__bss_start));

  /* Full access to the FPU, CP10 and CP11. */
  SCB_CPACR |= 0xfu << 20;
  __asm__ volatile("dsb\nisb" ::: "memory");
  __asm__ volatile("msr msplim, %0" : : "r"(__stack_limit));
  /* Report memory management, bus, usage and secure faults as themselves
   * rather than as hard faults. */
  SCB_SHCSR |= 0xfu << 16;

  board_init();
  kmain();
}

typedef void (*vector)(void);

[[gnu::section(".vectors"), gnu::used]] static const vector vectors[16 + IRQ_COUNT] = {
    [0] = (vector)__stack_top,
    [1] = reset_handler,
    [2] = unexpected_handler, /* NMI */
    [3] = fault_handler,      /* hard fault */
    [4] = fault_handler,      /* memory management */
    [5] = fault_handler,      /* bus fault */
    [6] = fault_handler,      /* usage fault */
    [7] = fault_handler,      /* secure fault */
    [11] = unexpected_handler, /* SVCall */
    [12] = unexpected_handler, /* debug monitor */
    [14] = unexpected_handler, /* PendSV */
    [15] = systick_handler,
    [16 ... 16 + UART0_RX_IRQ - 1] = unexpected_handler,
    [16 + UART0_RX_IRQ] = uart0_rx_handler,
    [16 + UART0_RX_IRQ + 1 ... 16 + IRQ_COUNT - 1] = unexpected_handler,
};
