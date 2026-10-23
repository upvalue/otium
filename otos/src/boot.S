/* OpenSBI enters here in S-mode with a0 = hart id and a1 = device tree. */
  .section .text.boot
  .globl _start
_start:
  .option push
  .option norelax
  la gp, __global_pointer$
  .option pop
  la sp, __stack_top

  /* Turn the FPU on (sstatus.FS = Initial) before any compiled code runs. */
  li t0, (1 << 13)
  csrs sstatus, t0
  fscsr zero

  la t0, trap_entry
  csrw stvec, t0

  la t0, __bss_start
  la t1, __bss_end
1:
  bgeu t0, t1, 2f
  sw zero, 0(t0)
  addi t0, t0, 4
  j 1b
2:
  call kmain
3:
  wfi
  j 3b

  /* Any trap is fatal for now: report it and stop. */
  .align 4
trap_entry:
  la sp, __stack_top
  csrr a0, scause
  csrr a1, sepc
  csrr a2, stval
  call kernel_trap
4:
  wfi
  j 4b
