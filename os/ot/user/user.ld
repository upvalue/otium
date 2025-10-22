ENTRY(start)

SECTIONS {
    . = 0x1000000;

    /* machine code */
    .text :{
        __text_start = .;
        KEEP(*(.text.start));
        *(.text .text.*);
        __text_end = .;
    }

    /* read-only data */
    .rodata : ALIGN(0x1000) {
        __rodata_start = .;
        *(.rodata .rodata.*);
        __rodata_end = .;
    }

    /* data with initial values */
    .data : ALIGN(0x1000) {
        __data_start = .;
        *(.data .data.*);
        __data_end = .;
    }

    /* data that should be zero-filled at startup */
    .bss : ALIGN(0x1000) {
        __bss_start = .;
        *(.bss .bss.* .sbss .sbss.*);

        . = ALIGN(16);
        . += 32 * 1024; /* 32KB */
        __stack_top = .;
        __bss_end = .;
09
       ASSERT(. < 0x1800000, "too large executable");
    }
}