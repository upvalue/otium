.section .text.boot
.global boot
boot:
  la sp, __stack_top
  j kernel_main

.section .bss
.global sbss
sbss:
.global ebss
ebss:
