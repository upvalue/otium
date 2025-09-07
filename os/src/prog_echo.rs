use crate::kernel;

pub fn prog_echo() -> ! {
  loop {
    kernel::print_str("> ");
    let str = kernel::prompt();
    kernel::print_str(str);
    kernel::print_str("\n");
  }
}
