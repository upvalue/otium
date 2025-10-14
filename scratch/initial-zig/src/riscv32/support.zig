// switch_context
pub fn switch_context(prev_sp: *u32, next_sp: *u32) callconv(.naked) noreturn {
    asm volatile (
    // Allocate stack space for pointers
        \\ addi sp, sp, -13 * 4
        \\ sw ra, 0 * 4(sp)
        \\ sw s0, 1 * 4(sp)
        \\ sw s1, 2 * 4(sp)
        \\ sw s2, 3 * 4(sp)
        \\ sw s3, 4 * 4(sp)
        \\ sw s4, 5 * 4(sp)
        \\ sw s5, 6 * 4(sp)
        \\ sw s6, 7 * 4(sp)
        \\ sw s7, 8 * 4(sp)
        \\ sw s8, 9 * 4(sp)
        \\ sw s9, 10 * 4(sp)
        \\ sw s10, 11 * 4(sp)
        \\ sw s11, 12 * 4(sp)
        // Switch stack pointer
        \\ sw sp, (a0)
        \\ lw sp, (a1)
        // Restore registers
        \\ lw ra, 0 * 4(sp)
        \\ lw s0, 1 * 4(sp)
        \\ lw s1, 2 * 4(sp)
        \\ lw s2, 3 * 4(sp)
        \\ lw s3, 4 * 4(sp)
        \\ lw s4, 5 * 4(sp)
        \\ lw s5, 6 * 4(sp)
        \\ lw s6, 7 * 4(sp)
        \\ lw s7, 8 * 4(sp)
        \\ lw s8, 9 * 4(sp)
        \\ lw s9, 10 * 4(sp)
        \\ lw s10, 11 * 4(sp)
        \\ lw s11, 12 * 4(sp)
        // Restore stack pointer
        \\ addi sp, sp, 13 * 4
        \\ ret
    );
}
