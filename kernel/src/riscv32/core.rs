use core::panic::PanicInfo;

core::arch::global_asm!(include_str!("boot.s"));

unsafe extern "C" {
  fn hello();
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
