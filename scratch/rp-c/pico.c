/**
 * Register access. 
 * See [page 26](https://datasheets.raspberrypi.com/rp2350/rp2350-datasheet.pdf#page=27) of the datasheet
 *
 * Basically, by doing some magical memory stuff to a specific address, we can modify registers.
 * Register here doesn't refer to the base registers available to assembly like r0, r1 etc, but also
 * to APB ("advanced peripheral bus") registers. APB registers are how we interact with other
 * components on the device, like general purpose I/O pins and UART (universal asynchronous
 * receiver/transmitter, serial monitor).
 */


/**
 * 
 */
#define REG(x) (*((volatile uint32_t*)(x)))

#define REG_ALIAS_RW_BITS 0x0000
#define REG_ALIAS_XOR_BITS 0x1000
#define REG_ALIAS_SET_BITS 0x2000
#define REG_ALIAS_CLR_BITS 0x3000


