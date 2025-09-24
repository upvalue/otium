// main.rs - main demo
#![no_std]
#![no_main]

extern crate alloc;

use embedded_alloc::Heap;

#[global_allocator]
static ALLOCATOR: Heap = Heap::empty();

// Alias for our HAL crate
use rp235x_hal as hal;

// Some things we need
use core::fmt::Write;
use heapless::String;
use heapless::Vec;

// USB Device support
use usb_device::{class_prelude::*, prelude::*};

// USB Communications Class Device support
use usbd_serial::SerialPort;

/// Tell the Boot ROM about our application
#[unsafe(link_section = ".start_block")]
#[used]
pub static IMAGE_DEF: hal::block::ImageDef = hal::block::ImageDef::secure_exe();

/// External high-speed crystal on the Raspberry Pi Pico 2 board is 12 MHz.
/// Adjust if your board has a different frequency
const XTAL_FREQ_HZ: u32 = 12_000_000u32;

mod psram;

use core::panic::PanicInfo;
use core::sync::atomic::{self, Ordering};
use hal::fugit::RateExtU32;

#[inline(never)]
#[panic_handler]
fn panic(_info: &PanicInfo) -> ! {
    // Note: Setting up USB in a panic handler is complex due to:
    // 1. USB clock requirements that can't be easily recreated
    // 2. USB peripheral may already be in use
    // 3. System state may be corrupted
    //
    // A more robust approach would be to:
    // - Use a dedicated UART for debug output
    // - Blink an LED in a pattern to indicate panic
    // - Write to persistent storage if available
    //
    // For this demo, we'll just spin to avoid undefined behavior
    loop {
        atomic::compiler_fence(Ordering::SeqCst);
    }
}

/// Entry point to our bare-metal application.
///
/// The `#[hal::entry]` macro ensures the Cortex-M start-up code calls this function
/// as soon as all global variables and the spinlock are initialised.
///
/// The function configures the rp235x peripherals, then writes to the UART in
/// an infinite loop.
#[hal::entry]
fn main() -> ! {
    // Set up allocator heap
    {
        /*
        const PSRAM_ADDRESS: usize = 0x11000000;
        unsafe { ALLOCATOR.init(PSRAM_ADDRESS, psram_size as usize) }
        */
    }
    // Grab our singleton objects
    let mut pac = hal::pac::Peripherals::take().unwrap();

    // Set up the watchdog driver - needed by the clock setup code
    let mut watchdog = hal::Watchdog::new(pac.WATCHDOG);

    // Configure the clocks
    let clocks = hal::clocks::init_clocks_and_plls(
        XTAL_FREQ_HZ,
        pac.XOSC,
        pac.CLOCKS,
        pac.PLL_SYS,
        pac.PLL_USB,
        &mut pac.RESETS,
        &mut watchdog,
    )
    .unwrap();

    let timer = hal::Timer::new_timer0(pac.TIMER0, &mut pac.RESETS, &clocks);

    // Set up the USB driver
    let usb_bus = UsbBusAllocator::new(hal::usb::UsbBus::new(
        pac.USB,
        pac.USB_DPRAM,
        clocks.usb_clock,
        true,
        &mut pac.RESETS,
    ));

    // Set up the USB Communications Class Device driver
    let mut serial = SerialPort::new(&usb_bus);

    // Create a USB device with a fake VID and PID
    let mut usb_dev = UsbDeviceBuilder::new(&usb_bus, UsbVidPid(0x16c0, 0x27dd))
        .strings(&[StringDescriptors::default()
            .manufacturer("Fake company")
            .product("Serial port")
            .serial_number("rpscratch")])
        .unwrap()
        .max_packet_size_0(64)
        .unwrap()
        .device_class(2) // from: https://www.usb.org/defined-class-codes
        .build();

    // set up gpio pins
    let sio = hal::Sio::new(pac.SIO);

    let pins = hal::gpio::Pins::new(
        pac.IO_BANK0,
        pac.PADS_BANK0,
        sio.gpio_bank0,
        &mut pac.RESETS,
    );

    // Initialize PSRam

    let _ = pins.gpio8.into_function::<hal::gpio::FunctionXipCs1>();
    let psram_size = psram::detect_psram(&pac.QMI);
    /*let psram_size = psram::psram_init(
        clocks.peripheral_clock.get_freq().to_Hz(),
        &pac.QMI,
        &pac.XIP_CTRL,
    );
    */

    let mut ln: Vec<u8, 512> = Vec::new();
    let mut test_txt: String<64> = String::new();
    test_txt.push_str("test");

    let mut said_hello = false;
    loop {
        // A welcome message at the beginning
        if !said_hello && timer.get_counter().ticks() >= 2_000_000 {
            said_hello = true;
            let _ = serial.write(b"channel open\r\n");

            let time = timer.get_counter().ticks();
            let mut text: String<64> = String::new();
            writeln!(&mut text, "Current timer ticks: {time}\r\n").unwrap();

            // This only works reliably because the number of bytes written to
            // the serial port is smaller than the buffers available to the USB
            // peripheral. In general, the return value should be handled, so that
            // bytes not transferred yet don't get lost.
            let _ = serial.write(text.as_bytes());
        }

        // Check for new data
        if usb_dev.poll(&mut [&mut serial]) {
            let mut buf = [0u8; 64];
            match serial.read(&mut buf) {
                Err(_e) => {
                    // Do nothing
                }
                Ok(0) => {
                    // Do nothing
                }
                Ok(count) => {
                    for i in 0..count {
                        let b = buf[i];
                        if b == b'\r' {
                            let _ = serial.write(b"\r\n");
                            let _ = serial.write(ln.as_slice());
                            let _ = serial.write(b"\r\n");
                            ln.clear();
                            continue;
                        }
                        let err = ln.push(b);
                        match err {
                            Ok(_) => {
                                let _ = serial.write(&[b]);
                            }
                            Err(_e) => {
                                break;
                            }
                        }
                    }
                }
            }
        }
    }
}

/// Program metadata for `picotool info`
#[unsafe(link_section = ".bi_entries")]
#[used]
pub static PICOTOOL_ENTRIES: [hal::binary_info::EntryAddr; 5] = [
    hal::binary_info::rp_cargo_bin_name!(),
    hal::binary_info::rp_cargo_version!(),
    hal::binary_info::rp_program_description!(c"USB Serial Example"),
    hal::binary_info::rp_cargo_homepage_url!(),
    hal::binary_info::rp_program_build_attribute!(),
];

// End of file
