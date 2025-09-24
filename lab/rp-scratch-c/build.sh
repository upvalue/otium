set -eux 
# Compile object files with debug info
#arm-none-eabi-gcc -mcpu=cortex-m0plus -mthumb -g -c blink.c -o blink.o
arm-none-eabi-gcc -mcpu=cortex-m0plus -mthumb -g -c boot.S -o boot.o

# Link with custom linker script and keep debug info
arm-none-eabi-ld -T linker.ld boot.o -o blink.elf 

# Convert to UF2 format for Pico
picotool uf2 convert blink.elf blink.uf2
