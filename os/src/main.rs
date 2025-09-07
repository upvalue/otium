#![no_std]
#![no_main]

#[cfg(target_arch = "riscv32")]
#[path = "./riscv32/core.rs"]
mod kernel;

#[cfg(target_arch = "wasm32")]
#[path = "./wasm/core.rs"]
mod kernel;
