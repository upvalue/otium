#include "ot/shared/address.hpp"
#include "ot/user/user.hpp"

extern char __stack_top[];

__attribute__((noreturn)) extern "C" void exit(void) {
  for (;;)
    ;
}

int syscall(int sysno, int arg0, int arg1, int arg2) {
  register int a0 __asm__("a0") = arg0;
  register int a1 __asm__("a1") = arg1;
  register int a2 __asm__("a2") = arg2;
  register int a3 __asm__("a3") = sysno;

  __asm__ __volatile__("ecall"
                       : "=r"(a0)
                       : "r"(a0), "r"(a1), "r"(a2), "r"(a3)
                       : "memory");

  return a0;
}

extern "C" {

int oputchar(char ch) { return syscall(OU_PUTCHAR, ch, 0, 0); }
void ou_exit(void) { syscall(OU_EXIT, 0, 0, 0); }
int ogetchar(void) { return syscall(OU_GETCHAR, 0, 0, 0); }
void ou_yield(void) { syscall(OU_YIELD, 0, 0, 0); }
void *ou_alloc_page(void) { return (void *)syscall(OU_ALLOC_PAGE, 0, 0, 0); }

__attribute__((section(".text.start"))) __attribute__((naked)) void
start(void) {
  __asm__ __volatile__("mv sp, %[stack_top] \n"
                       "call user_program_main           \n"
                       "call exit           \n" ::[stack_top] "r"(__stack_top));
}

} // extern "C"

PageAddr ou_get_sys_page(int type) {
  return PageAddr(syscall(OU_GET_SYS_PAGE, type, 0, 0));
}

PageAddr ou_get_arg_page(void) { return ou_get_sys_page(OU_SYS_PAGE_ARG); }
PageAddr ou_get_comm_page(void) { return ou_get_sys_page(OU_SYS_PAGE_COMM); }

int ou_io_puts(char *str, int size) {
  PageAddr comm_page = ou_get_comm_page();
  if (comm_page.is_null()) {
    return 0;
  }
  memcpy(comm_page.as<char>(), str, size);
  return syscall(OU_IO_PUTS, (int)size, 0, 0);
}
