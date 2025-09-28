// prog_echo.rs - An echo program
use crate::arch;

pub fn prog_echo() -> ! {
  loop {
    arch::print_str("> ");
    // let str = arch::prompt();
    // arch::print_str(str);
    arch::print_str("\n");
  }
}
