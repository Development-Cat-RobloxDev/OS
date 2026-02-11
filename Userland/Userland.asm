BITS 64
global _start

%define SYSCALL_SERIAL_PUTCHAR   1
%define SYSCALL_SERIAL_PUTS      2

section .text
_start:
    lea rdi, [rel msg]
    mov rax, SYSCALL_SERIAL_PUTS
    syscall

.loop:
    pause
    jmp .loop

section .rodata
msg:
    db "Hello from userland via syscall!", 10, 0
