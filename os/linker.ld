ENTRY(_start)

MEMORY {
    RAM : ORIGIN = 0x80200000, LENGTH = 128M
}

SECTIONS {
    .text : {
        *(.text.init)
        *(.text .text.*)
    } > RAM

    .rodata : {
        *(.rodata .rodata.*)
    } > RAM

    .data : {
        *(.data .data.*)
    } > RAM

    .bss : {
        *(.bss .bss.*)
        . = ALIGN(4);
        stack_bottom = .;
        . += 0x4000; /* 16KB stack */
        stack_top = .;
    } > RAM

    /DISCARD/ : {
        *(.eh_frame)
    }
}