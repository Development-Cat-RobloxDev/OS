all:
	nasm -f bin BootLoader/BootLoader.asm -o Build/BootLoader.bin
	i686-elf-gcc -ffreestanding -c Kernel/Kernel_Entry.c -o Build/Kernel_Entry.o
	i686-elf-gcc -ffreestanding -c Kernel/Kernel.c -o Build/Kernel.o
	i686-elf-ld -T Kernel/kernel.ld -o Build/Kernel.bin Build/Kernel_Entry.o Build/Kernel.o --oformat binary
	cat Build/BootLoader.bin Build/Kernel.bin > Build/OS.img

run:
	qemu-system-i386 -fda Build/OS.img


.PHONY: all run clean