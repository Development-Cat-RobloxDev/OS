[BITS 16]
[ORG 0x7C00]

start:
    mov [boot_drive], dl

    xor ax, ax
    mov ss, ax
    mov sp, 0x7C00

    mov ah, 0x0E
    mov al, 'A'
    int 0x10

    mov ah, 0x02
    mov al, 1
    mov ch, 0
    mov cl, 2
    mov dh, 0
    mov dl, [boot_drive]
    mov ax, 0x1000
    mov es, ax
    xor bx, bx
    int 0x13

    cli
    hlt

boot_drive db 0

times 510 - ($ - $$) db 0
dw 0xAA55
