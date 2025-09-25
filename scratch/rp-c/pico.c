/**
 * Register access primitives.
 * See [page
 * 26](https://datasheets.raspberrypi.com/rp2350/rp2350-datasheet.pdf#page=27)
 * of the datasheet
 *
 * Basically, by doing some magical memory stuff to a specific address, we can
 * modify registers. Register here doesn't refer to the base registers available
 * to assembly like r0, r1 etc, but also to APB ("advanced peripheral bus")
 * registers. APB registers are how we interact with other components on the
 * device, like general purpose I/O pins and UART (universal asynchronous
 * receiver/transmitter, serial monitor).
 *
 * Since we'll be doing this all the time we define a set of macros for this
 */

/**
 * REG(x) dereferences a memory address
 */
#define REG(x) (*((volatile unsigned int *)(x)))

#define REG_ALIAS_RW_BITS 0x0000
#define REG_ALIAS_XOR_BITS 0x1000
#define REG_ALIAS_SET_BITS 0x2000
#define REG_ALIAS_CLR_BITS 0x3000

/**
 * Bus endpoints. See [page
 * 32](https://datasheets.raspberrypi.com/rp2350/rp2350-datasheet.pdf#page=32)
 * of the datasheet. This page is where the _BASE constants come from. This
 * table also links out to the individual sections for each bus.
 */

/**
 * Resets ([chapter
 * 7(https://datasheets.raspberrypi.com/rp2350/rp2350-datasheet.pdf#page=495)))
 * of the datasheet.
 *
 * We use the reset base to make subsystem resets. Basically letting the system
 * know that we're ready to use GPIO, UART and so on, by for each of those
 * subsystems clearing the FRCE_ON ("force on") reset register and then setting
 * the RESET_DONE register which... resets the reset, making the subsystem ready
 * for our use.
 */
#define RESETS_BASE 0x40020000
#define RESETS_DONE_OFFSET 0x8

#define RESET_IO_BANK0_BITS (1 << 6)
#define RESET_PADS_BANK0_BITS (1 << 9)
#define RESET_PIO0_BITS (1 << 11)
#define RESET_PLL_SYS_BITS (1 << 14)
#define RESET_PWM_BITS (1 << 16)
#define RESET_TIMER0_BITS (1 << 23)
#define RESET_UART0_BITS (1 << 26)
#define RESET_UART1_BITS (1 << 27)

/**
 * The general purpose I/O bank ([chapter
 * 9](https://datasheets.raspberrypi.com/rp2350/rp2350-datasheet.pdf#page=588))
 * is a set of pins we can manipulate to interact with various components on the
 * board as well as ones we might choose to connect (for example if we e.g. a
 * soldered an LED circuit to one of the pins).
 *
 * Despite the name, in some cases GPIO pins are constrained in what they can do
 * based on the chip's design. See the table in the chapter for an exact list of
 * functionality.
 */
#define IO_BANK0_BASE 0x40028000
#define IO_BANK0_GPIO0_CTRL_OFFSET 0x4 + 0 * 0x8
#define IO_BANK0_GPIO1_CTRL_OFFSET 0x4 + 1 * 0x8
#define IO_BANK0_GPIO7_CTRL_OFFSET 0x4 + 7 * 0x8
#define IO_BANK0_GPIO23_CTRL_OFFSET 0x4 + 23 * 0x8
#define IO_BANK0_GPIO24_CTRL_OFFSET 0x4 + 24 * 0x8
#define IO_BANK0_GPIO25_CTRL_OFFSET 0x4 + 25 * 0x8
#define GPIO_FUNC_UART 2
#define GPIO_FUNC_PWM 4
#define GPIO_FUNC_SIO 5
#define GPIO_FUNC_PIO0 6

#define PADS_BANK0_BASE 0x40038000
#define PADS_BANK0_GPIO0_OFFSET 0x4
#define PADS_BANK0_GPIO1_OFFSET 0x4 + 1 * 0x4
#define PADS_BANK0_GPIO7_OFFSET 0x4 + 7 * 0x4
#define PADS_BANK0_GPIO23_OFFSET 0x4 + 23 * 0x4
#define PADS_BANK0_GPIO24_OFFSET 0x4 + 24 * 0x4
#define PADS_BANK0_GPIO25_OFFSET 0x4 + 25 * 0x4
#define PADS_BANK0_GPIO0_ISO_BITS 1 << 8
#define PADS_BANK0_GPIO0_OD_BITS 1 << 7
#define PADS_BANK0_GPIO0_IE_BITS 1 << 6
#define PADS_BANK0_GPIO0_PUE_BITS 1 << 3
#define PADS_BANK0_GPIO0_SCHMITT_BITS 1 << 1

#define SIO_BASE 0xd0000000
#define SIO_CPUID_OFFSET 0x0
#define SIO_GPIO_OE_CLR_OFFSET 0x40
#define SIO_GPIO_OUT_CLR_OFFSET 0x20
#define SIO_GPIO_OE_SET_OFFSET 0x38
#define SIO_GPIO_OUT_OFFSET 0x10
#define SIO_GPIO_OUT_XOR_OFFSET 0x28
#define SIO_FIFO_ST_OFFSET 0x50
#define SIO_FIFO_ST_VLD_BITS 1 << 0
#define SIO_FIFO_ST_RDY_BITS 1 << 1
#define SIO_FIFO_WR_OFFSET 0x54
#define SIO_FIFO_RD_OFFSET 0x58
