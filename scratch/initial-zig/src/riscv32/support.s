.section .text.boot
.global kernel_enter
kernel_enter:
  # Set up stack top and call main
  la sp, __stack_top
  j kernel_main

.section .bss
.global sbss
sbss:
.global ebss
ebss:
