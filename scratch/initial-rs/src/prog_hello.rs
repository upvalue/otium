use crate::arch;

pub fn prog_hello() {
  arch::print_str("Hello, world!\n");
  arch::exit();
}
