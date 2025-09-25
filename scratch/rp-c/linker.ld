/**
 * # rp2350 from scratch
 * This documentation is generated with [litsdoc](https://github.com/upvalue/litsdoc)
 *
 * It's largely derived from the [RP2350 datasheet](https://datasheets.raspberrypi.com/rp2350/rp2350-datasheet.pdf)
 * 
 * # Linker script
 * The first file is the linker script which describes the layout
 * of our program in memory. [Everything you never wanted to know about linker script](https://mcyoung.xyz/2021/06/01/linker-script/) was very helpful
 * and good to consult directly. The linker script will actually be used at the very end of the compilation process, once our code has been
 * compiled to ARM binary code.
 */

/* 
 * The MEMORY statement controls how memory addresses are allocated within our program. In higher level programming, when
 * we talk about memory, we talk about it as though it's one contiguous area that lives in RAM.
 * 
 * On this system (and in other low-level contexts), addresses in memory can have special meanings which are handled at the layers
 * beneath us. For example, we might load from a memory address and instead of reading from the system's (relatively low) RAM it
 * might be read from the much larger flash memory. The system handles this transparently for us, but it's up to us to make sure we
 * use the right addresses and use supported operations with those addresses (some memory for example can't contain executable code).
 * 
 * Magic memory addresses and registers are also how we interact with system processes, setting up functionality like the GPIO pins
 * or PSRAM. At the point we're starting at, there is no code defined to handle stuff like this, though it'd normally be handled
 * by the SDK or hardware abstraction layer in a higher level program.
 */ 
MEMORY {
  /* The flash memory is where our program lives; in the datasheet it's called XIP (executable in place) and exposed at the 
   * memory address 0x1000000.
   *
   * Notice that we also define whether the memory can be (r)ead, e(x)ecuted or (w)ritten to
   */
  FLASH(rx): ORIGIN = 0x10000000, LENGTH = 2M

  /**
   * The RP2350 has 520kb of SRAM which we use for the program stack and a few other things.
   * Both of the memory addresses here come from [pg 30](https://datasheets.raspberrypi.com/rp2350/rp2350-datasheet.pdf#page=31) of the datasheet
   */
  SRAM(rwx): ORIGIN = 0x20000000, LENGTH = 520K
}


/**
 * The second portion of our linker script is SECTIONS; a section is a named block of data.
 * The actual data of these sections will be supplied either by us or the C compiler later, 
 * but this tells the linker how to lay them out when linking.
 */
SECTIONS {
  /**
   * The init section is the very beginning of our program. For us, it just contains
   * a few magical constants that will be explained later.
   */
  .init :{
    /** . is the "current location counter". What we're doing here is aligning it to a 4-byte boundary. */
    . = ALIGN(4);
    /** This basically says "take the .init section from the object files and place it here" */
    *(.init);
    /** And then whatever comes after this should also be 4-byte aligned */
    . = ALIGN(4);
  /**
   * Our program is put into flash memory with this directive.
   */
  } >FLASH

  /** 
   * The text section is where the bulk of our program code will go. We're also putting 
   * another section, rodata ("read only data") along with it. .rodata contains
   * constants.
   */
  .text :{
    . = ALIGN(4);
    /** This specific directive is saying put any section named .text and any section
      * beginning with .text (the `.text*` part) here */
    *(.text .text*);
    *(.rodata .rodata*);
    . = ALIGN(4);
  } >FLASH

  /**
   * .data stores global constants
   */
  .data :{ 
    . = ALIGN(4);
    *(.data .data*);
    . = ALIGN(4);

    /**
     * Notice that there is a different directive here: constants are 
     * loaded from flash memory, but stored in RAM (because they may
     * be written to)
     */
  } >SRAM AT>FLASH

  stack_start = ORIGIN(SRAM) + LENGTH(SRAM);
}

