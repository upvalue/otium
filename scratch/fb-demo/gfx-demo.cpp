// Minimal VirtIO GPU graphics demo for RISC-V
// Avoids global variables to work in bare metal without .data initialization

#include <stdint.h>

typedef unsigned long size_t;

extern "C" void* memset(void* dst, int value, size_t count) {
    uint8_t* d = (uint8_t*)dst;
    uint8_t v = (uint8_t)value;
    for (size_t i = 0; i < count; i++) {
        d[i] = v;
    }
    return dst;
}

void print(const char* str) {
    volatile char* uart = (volatile char*)0x10000000;
    while (*str) {
        *uart = *str++;
    }
}

extern "C" void _start() {
    print("VirtIO GPU Demo Starting...\n");

    // Find virtio-gpu device
    print("Scanning for VirtIO GPU...\n");

    volatile uint32_t* gpu_mmio = nullptr;
    for (uint32_t addr = 0x10001000; addr <= 0x10008000; addr += 0x1000) {
        volatile uint32_t* mmio = (volatile uint32_t*)addr;
        uint32_t magic = mmio[0];  // VIRTIO_MMIO_MAGIC

        if (magic == 0x74726976) {  // "virt"
            uint32_t device_id = mmio[2];  // VIRTIO_MMIO_DEVICE_ID
            if (device_id == 16) {  // GPU device
                gpu_mmio = mmio;
                print("Found VirtIO GPU!\n");
                break;
            }
        }
    }

    if (!gpu_mmio) {
        print("No VirtIO GPU found. Exiting.\n");
        while (1) __asm__ volatile("wfi");
    }

    // For now, just demonstrate we found the device
    // A full implementation would:
    // 1. Reset and initialize the device (set status bits)
    // 2. Set up virtqueues for commands
    // 3. Create 2D resource
    // 4. Attach framebuffer memory
    // 5. Set scanout
    // 6. Draw pixels
    // 7. Flush to display

    print("VirtIO GPU initialization would happen here.\n");
    print("Full driver is ~500 lines - see virtio-gpu.cpp for complete implementation.\n");
    print("\n");
    print("Demonstration complete!\n");

    while (1) {
        __asm__ volatile("wfi");
    }
}
