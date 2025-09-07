use core::panic::PanicInfo;

unsafe extern "C" {
    fn host_print(ptr: *const u8, len: usize);
}

pub fn print_str(s: &str) {
    unsafe {
        host_print(s.as_ptr(), s.len());
    }
}

#[unsafe(no_mangle)]
pub extern "C" fn start() -> () {
    print_str("Hi!\n");
}

#[panic_handler]
fn panic(_info: &PanicInfo) -> ! {
    loop {}
}
