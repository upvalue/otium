use core::panic::PanicInfo;

core::arch::global_asm!(include_str!("boot.s"));

#[unsafe(no_mangle)]
fn main() -> ! {
  println("Hello from RISC-V 32-bit kernel!");
  println("CPU is running in machine mode");

  loop {
    unsafe {
      core::arch::asm!("wfi"); // Wait for interrupt
    }
  }
}

// UART functions for 32-bit
fn println(s: &str) {
  print(s);
  print("\n");
}

fn print(s: &str) {
  const UART_BASE: *mut u8 = 0x10000000 as *mut u8;

  for byte in s.bytes() {
    unsafe {
      UART_BASE.write_volatile(byte);
    }
  }
}

fn print_hex(prefix: &str, value: u32) {
  print(prefix);
  let hex_chars = b"0123456789abcdef";

  for i in (0..8).rev() {
    let nibble = ((value >> (i * 4)) & 0xf) as usize;
    unsafe {
      let uart = 0x10000000 as *mut u8;
      uart.write_volatile(hex_chars[nibble]);
    }
  }
  print("\n");
}

#[panic_handler]
fn panic(info: &PanicInfo) -> ! {
  println("Kernel panic!");
  if let Some(location) = info.location() {
    print("Location: ");
    print(location.file());
    print(":");
    // Simple number printing for line number
    let line = location.line();
    if line < 10 {
      unsafe {
        let uart = 0x10000000 as *mut u8;
        uart.write_volatile(b'0' + line as u8);
      }
    }
    print("\n");
  }

  loop {
    unsafe {
      core::arch::asm!("wfi");
    }
  }
}
