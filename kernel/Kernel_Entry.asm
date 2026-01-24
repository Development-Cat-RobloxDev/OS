BITS 16
GLOBAL kernel_entry
EXTERN kmain

kernel_entry:
    cli

    ; セグメント初期化（超重要）
    xor ax, ax
    mov ds, ax
    mov es, ax
    mov ss, ax
    mov sp, 0x9000

    sti
    call kmain

.hang:
    hlt
    jmp .hang
