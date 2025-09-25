/**
 * An image (contiguous blob of code and data) is what our code will eventually be linked into
 * for uploading to the RP2350. This little bit of assembly takes care of putting a small amount
 * of data at the beginning of our image, which will be scanned by the bootloader to actually
 * get into our program.
 */
.section .init

/**
 * The first element of our program is the "vector table." The vector table is an ARM concept
 * that seems pretty load bearing. The
 * [M7](https://developer.arm.com/documentation/dui0646/c/The-Cortex-M7-Processor/Exception-model/Vector-table)
 * is not the processor we're using but seemed to contain the most straightforward explanation;
 * basically the vector table tells the system what to do in the case of an execption. 
 * 
 * In our case though, the [stage2
 * bootloader](https://raw.githubusercontent.com/raspberrypi/pico-sdk/a1438dff1d38bd9c65dbd693f0e5db4b9ae91779/src/rp2040/boot_stage2/boot2_is25lp080.S)
 * (already on system) uses it at the beginning of our program to determine where to jump to and how
 * to set the stack pointer.
 */ 
.word stack_start
.word main

/* 
 * Now we have an IMAGE_DEF or image definition. As far as I can tell, this is found by scanning
 * for the magic constant at the beginning (PICOBIN_BLOCK_MARKER_START)
 *
 * This can potentially be a somewhat complex structure encoding instructions about how the program
 * should be run and secured, but in our case, we put a few magic constants 
 * These are discussed in [section
 * 5.9.5](https://datasheets.raspberrypi.com/rp2350/rp2350-datasheet.pdf#page=429)
 * of the datasheet as the "minimum viable image metadata"
 */ 

// PICOBIN_BLOCK_MARKER_START; magic constant
.word 0xffffded3
// This value defines a little bit of information about our program that tells the system
// how to operate. Notably, it lets us select between ARM or RISCV
.word 0x10210142
// Not clear what this does. Just padding?
.word 0x000001ff
// Link to self (there is no additional block after this one)
.word 0x00000000
// PICOBIN_BLOCK_MARKER_END; magic constant
.word 0xab123579

