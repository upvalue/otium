.section .text.boot
.global boot
boot:
  # Set up stack top and call main
  la sp, __stack_top
  j kernel_main

.section .bss
.global sbss
sbss:
.global ebss
ebss:
