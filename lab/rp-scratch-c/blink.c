#include "pico.c"

#define IO_BANK0_BASE 0x40028000
#define PADS_BANK0_BASE 0x40038000
#define SIO_BASE 0xd0000000

// GPIO registers (simplified)
#define GPIO_CTRL_FUNCSEL_SIO 5
#define GPIO25_CTRL (*(volatile unsigned int*)(IO_BANK0_BASE + 0x0cc))
#define GPIO25_PAD (*(volatile unsigned int*)(PADS_BANK0_BASE + 0x68))

// SIO registers for GPIO output
#define SIO_GPIO_OE (*(volatile unsigned int*)(SIO_BASE + 0x020))
#define SIO_GPIO_OUT (*(volatile unsigned int*)(SIO_BASE + 0x010))

void delay(unsigned int count) {
    for (volatile unsigned int i = 0; i < count; i++);
}

int main() {
    // Configure GPIO25 (onboard LED) as SIO-controlled output
    GPIO25_PAD = (1 << 6) | (1 << 4); // Enable output, disable pulls
    GPIO25_CTRL = GPIO_CTRL_FUNCSEL_SIO; // Set function to SIO
    SIO_GPIO_OE |= (1 << 25); // Enable output on GPIO25
    
    // Blink LED forever
    while (1) {
        SIO_GPIO_OUT |= (1 << 25);   // LED on
        delay(1000000);
        SIO_GPIO_OUT &= ~(1 << 25);  // LED off
        delay(1000000);
    }
    
    return 0;
}

void _start() {
  (void) main();
}
