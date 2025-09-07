#![no_std]
#![no_main]

#[cfg(target_arch = "wasm32")]
#[path = "./wasm/core.rs"]
mod kernel;

#[cfg(target_arch = "riscv32")]
#[path = "./riscv32/core.rs"]
mod kernel;

#[cfg(feature = "prog_echo")]
mod prog_echo;

#[cfg(feature = "prog_echo")]
fn run_prog() {
  prog_echo::prog_echo();
}

#[unsafe(no_mangle)]
pub extern "C" fn kernel_enter() -> () {
  kernel::start();
  run_prog();
}
