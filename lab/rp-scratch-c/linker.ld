/**
 * # rp2350 from scratch
 * This documentation is generated with [litsdoc](https://github.com/upvalue/litsdoc)
 *
 * It's largely derived from the [RP2350 datasheet](https://datasheets.raspberrypi.com/rp2350/rp2350-datasheet.pdf)
 * 
 * # Linker script
 * The first file is the [linker script](https://mcyoung.xyz/2021/06/01/linker-script/) which describes the layout
 * of our program in memory.
 */

/* 
 * The MEMORY statement controls how memory addresses are allocated within our program. In higher level programming, when
 * we talk about memory, we talk about it as though it's one contiguous area that lives in RAM.
 * 
 * On this system (and in other low-level contexts), addresses in memory can have special meanings which are handled at the layers
 * beneath us. For example, we might load from a memory address and instead of reading from the system's (relatively low) RAM it
 * might be read from the much larger flash memory. The system handles this transparently for us, but it's up to us to make sure we
 * use the right addresses and use supported operations with those addresses.
 * 
 * Magic memory addresses and registers are also how we interact with system processes, setting up functionality like the GPIO pins
 * or PSRAM -- because at this level, there is no SDK and no user code written.
 */ 


MEMORY {
  /* The flash memory is where our program lives; in the datasheet it's called XIP (executable in place) and exposed at the 
   * memory address 0x1000000.
   *
   * Both of the values come from [pg 30](https://datasheets.raspberrypi.com/rp2350/rp2350-datasheet.pdf#page=31) of the datasheet
   */
  FLASH(rx): ORIGIN = 0x10000000, LENGTH = 2M

  SRAM(rwx): ORIGIN = 0x20000000, LENGTH = 512K
}

ENTRY(_start)

SECTIONS {
  . = ORIGIN(FLASH);
  .text : {
    *(.entry)
    *(.text)
  } >FLASH
}
