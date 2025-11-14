// Simple test to verify basic execution
#include <stdint.h>

// UART
#define UART_BASE 0x10000000
volatile uint8_t* uart = (uint8_t*)UART_BASE;

void putchar(char c) {
    *uart = c;
}

void print(const char* str) {
    while (*str) {
        putchar(*str++);
    }
}

extern "C" void _start() {
    // Setup stack
    __asm__ volatile("la sp, __stack_top");

    print("Hello from RISC-V!\n");
    print("Testing UART output...\n");

    // Check virtio devices
    print("Scanning for VirtIO devices...\n");
    for (uint32_t addr = 0x10001000; addr <= 0x10008000; addr += 0x1000) {
        volatile uint32_t* mmio = (volatile uint32_t*)addr;
        uint32_t magic = mmio[0];

        if (magic == 0x74726976) {
            print("Found device at 0x");
            // Simple hex print
            const char* hex = "0123456789ABCDEF";
            for (int i = 7; i >= 0; i--) {
                putchar(hex[(addr >> (i * 4)) & 0xF]);
            }
            print("\n");

            uint32_t device_id = mmio[2];
            print("  Device ID: 0x");
            for (int i = 7; i >= 0; i--) {
                putchar(hex[(device_id >> (i * 4)) & 0xF]);
            }
            print("\n");
        }
    }

    print("Scan complete.\n");

    while (1) {
        __asm__ volatile("wfi");
    }
}
