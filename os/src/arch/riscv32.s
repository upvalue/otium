.section .text.init
.global _start
_start:
    # Set the stack pointer
    la sp, stack_top
    
    # Clear BSS section
    la t0, sbss
    la t1, ebss
1:
    beq t0, t1, 2f
    sw zero, 0(t0)
    addi t0, t0, 4
    j 1b
2:
    # Jump to Rust main
    call main
    
.section .bss
.global sbss
sbss:
.global ebss
ebss:
