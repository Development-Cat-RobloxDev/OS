-- QEMU-RUN  :  qemu-system-x86_64 build/os.img

-- HOW TO BUILD?
nasm bootloader/bootloader.asm -f bin -o build/bootloader.bin
nasm bootloader/entry_kernel.asm -f elf -o build/entry_kernel.o

gcc -m16 -ffreestanding -fno-pic -fno-stack-protector \
    -nostdlib -c kernel/kernel.c -o build/kernel.o

ld -m elf_i386 linker.ld -o build/kernel.bin build/entry_kernel.o build/kernel.o binary

cat build/bootloader.bin build/kernel.bin > build/os.img

qemu-system-x86_64 build/os.img