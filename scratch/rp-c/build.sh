set -eux 
# Compile object files with debug info
arm-none-eabi-gcc -mcpu=cortex-m33 -mthumb -g -c boot.s -o obj/boot.o

arm-none-eabi-gcc -mcpu=cortex-m33 -mthumb -g -c main.c -o obj/main.o

# Link with custom linker script and keep debug info
arm-none-eabi-ld -g -T linker.ld obj/boot.o obj/main.o -o obj/main.elf

arm-none-eabi-objcopy -O binary obj/main.elf obj/main.bin

# Convert to UF2 format for Pico
picotool uf2 convert obj/main.bin obj/main.uf2
