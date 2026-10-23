OUTPUT_ARCH(riscv)
ENTRY(_start)

/* QEMU virt: RAM starts at 0x80000000 and OpenSBI occupies the first 2MB.
 * The size must match the -m flag given to QEMU. */
MEMORY {
  ram (rwx) : ORIGIN = 0x80200000, LENGTH = 256M - 2M
}

SECTIONS {
  .text : {
    KEEP(*(.text.boot))
    *(.text .text.*)
  } > ram

  .rodata : ALIGN(16) {
    *(.srodata .srodata.*)
    *(.rodata .rodata.*)
  } > ram

  .data : ALIGN(16) {
    *(.data .data.*)
    *(.got .got.*)
    __global_pointer$ = . + 0x800;
    *(.sdata .sdata.*)
  } > ram

  .bss (NOLOAD) : ALIGN(16) {
    __bss_start = .;
    *(.sbss .sbss.*)
    *(.bss .bss.*)
    *(COMMON)
    . = ALIGN(16);
    __bss_end = .;
  } > ram

  .stack (NOLOAD) : ALIGN(16) {
    . += 4M;
    __stack_top = .;
  } > ram

  .heap (NOLOAD) : ALIGN(4096) {
    __heap_start = .;
  } > ram
  __heap_end = ORIGIN(ram) + LENGTH(ram);

  /DISCARD/ : { *(.eh_frame .eh_frame_hdr) }
}
