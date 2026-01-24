all:
	nasm -f bin BootLoader/BootLoader.asm -o Build/BootLoader.bin
	nasm -f elf32 Kernel/Kernel_Entry.asm -o Build/Kernel_Entry.o
	i686-elf-gcc -ffreestanding -m16 -c Kernel/Kernel.c -o Build/Kernel.o
	i686-elf-ld -T Kernel/Kernel_Linker.ld -o Build/Kernel.elf Build/Kernel_Entry.o Build/Kernel.o
	i686-elf-objcopy -O binary Build/Kernel.elf Build/Kernel.bin
	cat Build/BootLoader.bin Build/Kernel.bin > Build/OS.img

run:
	qemu-system-i386 -drive format=raw,file=Build/OS.img

clean:
	rm -rf Build/*

.PHONY: all run clean
