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

  /* Interrupts save the caller-saved registers, run kernel_interrupt and
   * return. Exceptions are fatal: they reset sp, in case it caused the fault,
   * and report through kernel_fault. */
  .align 4
trap_entry:
  csrw sscratch, t0
  csrr t0, scause
  bltz t0, interrupt
  la sp, __stack_top
  csrr a0, scause
  csrr a1, sepc
  csrr a2, stval
  call kernel_fault
4:
  wfi
  j 4b

interrupt:
  csrr t0, sscratch
  addi sp, sp, -240
  sw ra, 0(sp)
  sw t0, 4(sp)
  sw t1, 8(sp)
  sw t2, 12(sp)
  sw t3, 16(sp)
  sw t4, 20(sp)
  sw t5, 24(sp)
  sw t6, 28(sp)
  sw a0, 32(sp)
  sw a1, 36(sp)
  sw a2, 40(sp)
  sw a3, 44(sp)
  sw a4, 48(sp)
  sw a5, 52(sp)
  sw a6, 56(sp)
  sw a7, 60(sp)
  fsd ft0, 64(sp)
  fsd ft1, 72(sp)
  fsd ft2, 80(sp)
  fsd ft3, 88(sp)
  fsd ft4, 96(sp)
  fsd ft5, 104(sp)
  fsd ft6, 112(sp)
  fsd ft7, 120(sp)
  fsd ft8, 128(sp)
  fsd ft9, 136(sp)
  fsd ft10, 144(sp)
  fsd ft11, 152(sp)
  fsd fa0, 160(sp)
  fsd fa1, 168(sp)
  fsd fa2, 176(sp)
  fsd fa3, 184(sp)
  fsd fa4, 192(sp)
  fsd fa5, 200(sp)
  fsd fa6, 208(sp)
  fsd fa7, 216(sp)
  frcsr t0
  sw t0, 224(sp)

  csrr a0, scause
  call kernel_interrupt

  lw t0, 224(sp)
  fscsr t0
  fld ft0, 64(sp)
  fld ft1, 72(sp)
  fld ft2, 80(sp)
  fld ft3, 88(sp)
  fld ft4, 96(sp)
  fld ft5, 104(sp)
  fld ft6, 112(sp)
  fld ft7, 120(sp)
  fld ft8, 128(sp)
  fld ft9, 136(sp)
  fld ft10, 144(sp)
  fld ft11, 152(sp)
  fld fa0, 160(sp)
  fld fa1, 168(sp)
  fld fa2, 176(sp)
  fld fa3, 184(sp)
  fld fa4, 192(sp)
  fld fa5, 200(sp)
  fld fa6, 208(sp)
  fld fa7, 216(sp)
  lw ra, 0(sp)
  lw t0, 4(sp)
  lw t1, 8(sp)
  lw t2, 12(sp)
  lw t3, 16(sp)
  lw t4, 20(sp)
  lw t5, 24(sp)
  lw t6, 28(sp)
  lw a0, 32(sp)
  lw a1, 36(sp)
  lw a2, 40(sp)
  lw a3, 44(sp)
  lw a4, 48(sp)
  lw a5, 52(sp)
  lw a6, 56(sp)
  lw a7, 60(sp)
  addi sp, sp, 240
  sret
