.PHONY: all run clean image

ARCH := x86_64
KCC  := x86_64-linux-gnu-gcc
KLD  := x86_64-linux-gnu-ld
NASM := nasm

BUILD_DIR := Build
IMAGE_DIR := Image
IMAGE     := $(IMAGE_DIR)/disk.img
OVMF_CODE := /usr/share/OVMF/OVMF_CODE_4M.fd
KERNEL_ELF := $(BUILD_DIR)/Kernel_Main.ELF
BOOTX64_EFI := $(BUILD_DIR)/BOOTX64.EFI

KERNEL_CFLAGS := -IKernel -IThirdParty -ffreestanding -fno-stack-protector -fno-pic -mno-red-zone -nostdlib -nostartfiles -nodefaultlibs -Wall -Wextra
KERNEL_LDFLAGS := -T Kernel/Kernel_Main.ld -nostdlib

KERNEL_OBJS := \
    $(BUILD_DIR)/Kernel_Main.o \
    $(BUILD_DIR)/Memory_Main.o \
    $(BUILD_DIR)/Memory_Utils.o \
    $(BUILD_DIR)/Other_Utils.o \
    $(BUILD_DIR)/Paging_Main.o \
    $(BUILD_DIR)/Paging_ASM.o \
    $(BUILD_DIR)/FrameBuffer_Main.o \
    $(BUILD_DIR)/FrameBuffer_Cursor.o \
    $(BUILD_DIR)/Font_Main.o \
    $(BUILD_DIR)/IDT_Main.o \
    $(BUILD_DIR)/idt_load.o \
    $(BUILD_DIR)/WindowManager_Main.o \
    $(BUILD_DIR)/Mouse_Main.o \
    $(BUILD_DIR)/IO_Main.o \
    $(BUILD_DIR)/FAT32_Main.o


all: $(BOOTX64_EFI) $(KERNEL_ELF)

# ========================================
# UEFI Loader
# ========================================
$(BUILD_DIR)/Loader.o: BootLoader/Loader.c
	mkdir -p $(BUILD_DIR)
	$(KCC) -I/usr/include/efi -I/usr/include/efi/x86_64 -I/usr/include/efi/protocol \
	    -fno-stack-protector -fpic -fshort-wchar -mno-red-zone -ffreestanding \
	    -Wall -Wextra -DEFI_FUNCTION_WRAPPER \
	    -c $< -o $@

$(BUILD_DIR)/BOOTX64.EFI: $(BUILD_DIR)/Loader.o
	ld -nostdlib -znocombreloc -T /usr/lib/elf_x86_64_efi.lds -shared -Bsymbolic \
	   /usr/lib/crt0-efi-x86_64.o $< \
	   /usr/lib/libefi.a \
	   /usr/lib/libgnuefi.a \
	   -o $@.so

	objcopy -j .text -j .sdata -j .data -j .dynamic \
	        -j .dynsym -j .rel -j .rela -j .reloc \
	        --target=efi-app-x86_64 $@.so $@
	rm -f $@.so

# ========================================
# カーネル C コンパイル
# ========================================
$(BUILD_DIR)/%.o: Kernel/%.c
	mkdir -p $(BUILD_DIR)
	$(KCC) $(KERNEL_CFLAGS) -c $< -o $@

# サブディレクトリ対応
$(BUILD_DIR)/Memory_Main.o: Kernel/Memory/Memory_Main.c
	mkdir -p $(BUILD_DIR)
	$(KCC) $(KERNEL_CFLAGS) -c $< -o $@

$(BUILD_DIR)/Memory_Utils.o: Kernel/Memory/Memory_Utils.c
	mkdir -p $(BUILD_DIR)
	$(KCC) $(KERNEL_CFLAGS) -c $< -o $@

$(BUILD_DIR)/Other_Utils.o: Kernel/Memory/Other_Utils.c
	mkdir -p $(BUILD_DIR)
	$(KCC) $(KERNEL_CFLAGS) -c $< -o $@

$(BUILD_DIR)/Paging_Main.o: Kernel/Paging/Paging_Main.c
	mkdir -p $(BUILD_DIR)
	$(KCC) $(KERNEL_CFLAGS) -c $< -o $@

$(BUILD_DIR)/FrameBuffer_Main.o: Kernel/FrameBuffer/FrameBuffer_Main.c
	mkdir -p $(BUILD_DIR)
	$(KCC) $(KERNEL_CFLAGS) -c $< -o $@

$(BUILD_DIR)/FrameBuffer_Cursor.o: Kernel/FrameBuffer/FrameBuffer_Cursor.c
	mkdir -p $(BUILD_DIR)
	$(KCC) $(KERNEL_CFLAGS) -c $< -o $@

$(BUILD_DIR)/Font_Main.o: Kernel/Font/Font_Main.c
	mkdir -p $(BUILD_DIR)
	$(KCC) $(KERNEL_CFLAGS) -DSTB_TRUETYPE_IMPLEMENTATION -c $< -o $@

$(BUILD_DIR)/IDT_Main.o: Kernel/IDT/IDT_Main.c
	mkdir -p $(BUILD_DIR)
	$(KCC) $(KERNEL_CFLAGS) -c $< -o $@

$(BUILD_DIR)/WindowManager_Main.o: Kernel/WindowManager/WindowManager_Main.c
	mkdir -p $(BUILD_DIR)
	$(KCC) $(KERNEL_CFLAGS) -c $< -o $@

$(BUILD_DIR)/Mouse_Main.o: Kernel/Mouse/Mouse_Main.c
	mkdir -p $(BUILD_DIR)
	$(KCC) $(KERNEL_CFLAGS) -c $< -o $@

$(BUILD_DIR)/IO_Main.o: Kernel/IO/IO_Main.c
	mkdir -p $(BUILD_DIR)
	$(KCC) $(KERNEL_CFLAGS) -c $< -o $@

$(BUILD_DIR)/FAT32_Main.o: Kernel/Drivers/FileSystem/FAT32/FAT32_Main.c
	mkdir -p $(BUILD_DIR)
	$(KCC) $(KERNEL_CFLAGS) -c $< -o $@

$(BUILD_DIR)/stb_wrapper.o: Kernel/ThirdParty/stb_wrapper.c
	mkdir -p $(BUILD_DIR)
	$(KCC) $(KERNEL_CFLAGS) -c $< -o $@

# アセンブリ
$(BUILD_DIR)/Paging_ASM.o: Kernel/Paging/Paging.asm
	mkdir -p $(BUILD_DIR)
	$(NASM) -f elf64 $< -o $@

$(BUILD_DIR)/idt_load.o: Kernel/IDT/idt_load.asm
	mkdir -p $(BUILD_DIR)
	$(NASM) -f elf64 $< -o $@

# ========================================
# カーネル ELF リンク
# ========================================
$(KERNEL_ELF): $(KERNEL_OBJS)
	$(KLD) $(KERNEL_LDFLAGS) $^ -o $@

# ========================================
# Disk image 作成
# ========================================
image: all
	mkdir -p $(IMAGE_DIR)
	dd if=/dev/zero of=$(IMAGE) bs=1M count=128
	parted $(IMAGE) --script \
		mklabel gpt \
		mkpart ESP fat32 1MiB 100% \
		set 1 esp on
	sudo losetup -Pf --show $(IMAGE) | while read LOOP; do \
	    sudo mkfs.fat -F32 $${LOOP}p1; \
	    sudo mount $${LOOP}p1 /mnt; \
	    sudo mkdir -p /mnt/EFI/BOOT; \
	    sudo cp $(BOOTX64_EFI) /mnt/EFI/BOOT/BOOTX64.EFI; \
	    sudo cp $(KERNEL_ELF) /mnt/EFI/BOOT/Kernel_Main.ELF; \
	    sudo cp Kernel/LOGO.PNG /mnt/LOGO.PNG; \
		sudo cp Kernel/NOTOSANS.TTF /mnt/NOTOSANS.TTF; \
	    sync; sudo umount /mnt; \
	    sudo losetup -d $$LOOP; \
	done

# ========================================
# QEMU 起動
# ========================================
run: image
	qemu-system-x86_64 \
		-m 512M \
		-drive if=pflash,format=raw,readonly=on,file=$(OVMF_CODE) \
		-drive format=raw,file=$(IMAGE) \
   		-usb \
   		-device usb-mouse \
   		-device usb-kbd \
		-serial stdio

# ========================================
# クリーン
# ========================================
clean:
	rm -rf $(BUILD_DIR) $(IMAGE_DIR)
