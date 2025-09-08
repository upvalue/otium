#![no_std]
#![no_main]

#[cfg(target_arch = "wasm32")]
#[path = "./wasm/core.rs"]
mod arch;

#[cfg(target_arch = "riscv32")]
#[path = "./riscv32/core.rs"]
mod arch;

#[cfg(feature = "prog_echo")]
mod prog_echo;

#[cfg(feature = "prog_echo")]
fn run_prog() {
  prog_echo::prog_echo();
}

#[cfg(feature = "prog_hello")]
mod prog_hello;

#[cfg(feature = "prog_hello")]
fn run_prog() {
  prog_hello::prog_hello();
}

#[unsafe(no_mangle)]
pub extern "C" fn kernel_enter() -> () {
  arch::print_str("kernel_enter called\n");
  arch::start();
  run_prog();
}
