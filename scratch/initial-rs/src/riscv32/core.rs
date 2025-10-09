use core::{ffi::c_long, panic::PanicInfo};

core::arch::global_asm!(include_str!("boot.s"));

unsafe extern "C" {
  fn hello();
}

pub struct SBIError {
  pub errc: isize,
}

#[unsafe(no_mangle)]
/**
 * Calls an OpenSBI function; for running on qemu
 */
fn sbi_call(
  a0: c_long,
  a1: c_long,
  a2: c_long,
  a3: c_long,
  a4: c_long,
  a5: c_long,
  fid: c_long,
  eid: c_long,
) -> Result<isize, SBIError> {
  let error: isize;
  let value: isize;
  unsafe {
    core::arch::asm!(
    "ecall",
    inout("a0") a0 => error,
    inout("a1") a1 => value,
    in("a2") a2,
    in("a3") a3,
    in("a4") a4,
    in("a5") a5,
    in("a6") fid,
    in("a7") eid,
    options(nostack, preserves_flags),
    )
  }

  if error == 0 {
    Ok(value)
  } else {
    Err(SBIError { errc: error })
  }
}

#[unsafe(no_mangle)]
pub fn start() -> () {
  println("Hello from RISC-V 32-bit kernel!");
  println("CPU is running in machine mode");

  unsafe {
    hello();
  }
}

// UART functions for 32-bit
fn println(s: &str) {
  print_str(s);
  print_str("\n");
}

pub fn print_str(s: &str) {
  const UART_BASE: *mut u8 = 0x10000000 as *mut u8;

  for byte in s.bytes() {
    unsafe {
      UART_BASE.write_volatile(byte);
    }
  }
}

#[panic_handler]
fn panic(info: &PanicInfo) -> ! {
  println("Kernel panic!");
  if let Some(location) = info.location() {
    print_str("Location: ");
    print_str(location.file());
    print_str(":");
    // Simple number printing for line number
    let line = location.line();
    if line < 10 {
      unsafe {
        let uart = 0x10000000 as *mut u8;
        uart.write_volatile(b'0' + line as u8);
      }
    }
    print_str("\n");
  }

  loop {
    unsafe {
      core::arch::asm!("wfi");
    }
  }
}

pub fn exit() -> ! {
  loop {
    unsafe {
      core::arch::asm!("wfi");
    }
  }
}
