use core::panic::PanicInfo;

unsafe extern "C" {
  fn host_print(ptr: *const u8, len: usize);
  fn host_readln(buffer: *mut u8, bufferlen: usize) -> bool;
}

use core::arch::wasm32 as wasm;

// Statically allocated 1MB heap for now
static mut HEAP: [u8; 1024 * 1024] = [0; 1024 * 1024];

static mut SCRATCH: [u8; 1024 * 1024] = [0; 1024 * 1024];
const SCRATCH_LENGTH: usize = 1024 * 1024;
static mut SCRATCH_IDX: usize = 0;

pub fn print_str(s: &str) {
  unsafe {
    host_print(s.as_ptr(), s.len());
  }
}

pub fn prompt() -> &'static str {
  unsafe {
    host_readln(core::ptr::addr_of_mut!(SCRATCH[0]), 1024 * 1024);

    // Find the length by looking for null terminator or newline
    let ptr = core::ptr::addr_of!(SCRATCH[0]);
    let mut len = 0;
    while len < SCRATCH_LENGTH {
      let byte = *ptr.add(len);
      if byte == 0 || byte == b'\n' || byte == b'\r' {
        break;
      }
      len += 1;
    }

    // Convert pointer to slice, then to str
    let slice = core::slice::from_raw_parts(ptr, len);
    core::str::from_utf8(slice).unwrap_or("")
  }
}

pub fn readln() {}

#[unsafe(no_mangle)]
pub extern "C" fn alloc(bytes: usize) -> *const u8 {
  unsafe {
    let ptr = SCRATCH_IDX;
    SCRATCH_IDX += bytes;
    return &SCRATCH[ptr];
  }
}

#[unsafe(no_mangle)]
pub extern "C" fn start() -> () {
  /*
  loop {
    print_str("> ");
    let str = prompt();
    print_str(str);
    print_str("\n");
  }*/
}

#[panic_handler]
fn panic(_info: &PanicInfo) -> ! {
  loop {}
}
