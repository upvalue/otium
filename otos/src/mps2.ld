/* QEMU's mps2-an505, laid out like the Fruit Jam's RP2350: code and read-only
 * data where its flash would be, data and the stack in 520 KB like its SRAM,
 * and the C heap in 8 MB like its PSRAM. These are the secure aliases, since
 * the kernel stays in the secure state it starts in. */
MEMORY {
  flash (rx) : ORIGIN = 0x10000000, LENGTH = 4M
  sram (rwx) : ORIGIN = 0x38000000, LENGTH = 520K
  psram (rw) : ORIGIN = 0x80000000, LENGTH = 8M
}

ENTRY(reset_handler)

STACK_SIZE = 64K;

SECTIONS {
  .text : {
    KEEP(*(.vectors))
    *(.text .text.*)
    *(.rodata .rodata.*)
    . = ALIGN(4);
  } > flash

  .ARM.exidx : { *(.ARM.exidx .ARM.exidx.*) } > flash

  /* Copied from flash to SRAM before kmain. */
  .data : ALIGN(4) {
    __data_start = .;
    *(.data .data.*)
    . = ALIGN(4);
    __data_end = .;
  } > sram AT > flash
  __data_load = LOADADDR(.data);

  .bss (NOLOAD) : ALIGN(4) {
    __bss_start = .;
    *(.bss .bss.*)
    *(COMMON)
    . = ALIGN(4);
    __bss_end = .;
  } > sram

  /* MSPLIM is set to the bottom, so an overflow faults instead of running
   * into .bss. */
  .stack (NOLOAD) : ALIGN(8) {
    __stack_limit = .;
    . += STACK_SIZE;
    __stack_top = .;
  } > sram

  /* picolibc's sbrk hands out this range. */
  __heap_start = ORIGIN(psram);
  __heap_end = ORIGIN(psram) + LENGTH(psram);
}
