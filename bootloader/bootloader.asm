org 0x7c00
bits 16

start:
    cli
    xor ax, ax
    mov ds, ax
    mov ss, ax
    mov sp, 0x7c00

    ; kernel を 0x100000 にロード
    mov ax, 0x1000
    mov es, ax
    xor bx, bx

    mov ah, 0x02
    mov al, 16          ; kernel size (sectors)
    mov ch, 0
    mov cl, 2
    mov dh, 0
    mov dl, 0x80
    int 0x13
    jc disk_error

    ; GDTセットアップ
    lgdt [gdt_desc]

    ; プロテクトモード有効化
    mov eax, cr0
    or eax, 1
    mov cr0, eax

    ; フラッシュジャンプ
    jmp CODE_SEL:pm_start

disk_error:
    hlt

; ---------------- GDT ----------------
gdt:
    dq 0
gdt_code:
    dw 0xffff, 0
    db 0, 10011010b, 11001111b, 0
gdt_data:
    dw 0xffff, 0
    db 0, 10010010b, 11001111b, 0

gdt_desc:
    dw gdt_end - gdt - 1
    dd gdt
gdt_end:

CODE_SEL equ gdt_code - gdt
DATA_SEL equ gdt_data - gdt

; -------------- protected mode --------------
bits 32
pm_start:
    mov ax, DATA_SEL
    mov ds, ax
    mov es, ax
    mov ss, ax
    mov esp, 0x90000

    jmp 0x100000

times 510-($-$$) db 0
dw 0xaa55
