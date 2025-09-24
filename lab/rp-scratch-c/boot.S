#define ACTIVITY_LED 9
#define BOOT_MODE 0x0

// 
.syntax unified
.cpu cortex-m33
.fpu fpv5-sp-d16
.thumb_func

.section .text

.global _start
.type _start,%function
_start:
  movs r5, #5
  b dead

dead:
  wfi
  b dead

.global literals
literals:
.ltorg

.end




