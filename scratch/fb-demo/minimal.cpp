// Absolute minimal test

extern "C" void _start() {
    // Direct UART access without globals
    volatile char* uart = (volatile char*)0x10000000;

    const char* msg = "Hello!\n";
    while (*msg) {
        *uart = *msg++;
    }

    while (1) {
        __asm__ volatile("wfi");
    }
}
