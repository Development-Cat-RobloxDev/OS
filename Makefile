ASM     = nasm
CC      = gcc
LD      = gld
QEMU    = qemu-system-x86_64

CFLAGS  = -ffreestanding -m32 -fno-pie -fno-stack-protector
LDFLAGS = -m elf_i386

BOOT_DIR   = bootloader
KERNEL_DIR = kernel
BUILD_DIR  = build

BOOT_BIN   = $(BUILD_DIR)/bootloader.bin
KERNEL_OBJ = $(BUILD_DIR)/kernel.o
OS_IMAGE   = $(BUILD_DIR)/os.img

all: $(OS_IMAGE)

$(OS_IMAGE): $(BOOT_BIN)
	cat $^ > $@

$(BOOT_BIN): $(BOOT_DIR)/bootloader.asm
	mkdir -p $(BUILD_DIR)
	$(ASM) -f bin $< -o $@

run: $(OS_IMAGE)
	$(QEMU) $(OS_IMAGE)

clean:
	rm -rf $(BUILD_DIR)
