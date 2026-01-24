BITS 16
ORG 0x7C00

start:
    cli
    xor ax, ax
    mov ds, ax
    mov es, ax
    mov ss, ax
    mov sp, 0x7A00     ; safer stack
    cld
    sti

    mov [boot_drive], dl

    mov si, k_load
    call print

    ; Load kernel (1 sector) to 0x1000:0000
    mov ax, 0x1000
    mov es, ax
    xor bx, bx

    mov ah, 0x02       ; BIOS read
    mov al, 1          ; sectors
    mov ch, 0
    mov cl, 2
    mov dh, 0
    mov dl, [boot_drive]
    int 0x13
    jc disk_error

    mov si, k_succ
    call print

    ; jump to kernel
    jmp 0x1000:0x0000

disk_error:
    mov si, k_fail
    call print
    hlt
    jmp $

; ----------------------------
print:
    mov ah, 0x0E
.loop:
    lodsb
    test al, al
    jz .done
    int 0x10
    jmp .loop
.done:
    ret

; ----------------------------
boot_drive db 0

k_load db "Initializing Kernel...", 13, 10, 0
k_succ db "Kernel loaded successfully!", 13, 10, 0
k_fail db "Kernel failed to load!", 13, 10, 0

times 510-($-$$) db 0
dw 0xAA55
