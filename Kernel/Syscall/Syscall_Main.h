#pragma once
#include <stdint.h>

#define SYSCALL_SERIAL_PUTCHAR  1
#define SYSCALL_SERIAL_PUTS     2
#define SYSCALL_PROCESS_CREATE  3
#define SYSCALL_PROCESS_YIELD   4
#define SYSCALL_PROCESS_EXIT    5
#define SYSCALL_THREAD_CREATE   6
#define SYSCALL_DRAW_PIXEL      10
#define SYSCALL_DRAW_FILL_RECT  11
#define SYSCALL_DRAW_PRESENT    12
#define SYSCALL_FILE_OPEN       20
#define SYSCALL_FILE_READ       21
#define SYSCALL_FILE_WRITE      22
#define SYSCALL_FILE_CLOSE      23
#define SYSCALL_USER_KMALLOC    24
#define SYSCALL_USER_KFREE      25

#define SYSCALL_FRAME_RAX 0
#define SYSCALL_FRAME_RDX 1
#define SYSCALL_FRAME_RSI 2
#define SYSCALL_FRAME_RDI 3
#define SYSCALL_FRAME_R8  4
#define SYSCALL_FRAME_R9  5
#define SYSCALL_FRAME_R10 6
#define SYSCALL_FRAME_R12 7
#define SYSCALL_FRAME_R13 8
#define SYSCALL_FRAME_R14 9
#define SYSCALL_FRAME_R15 10
#define SYSCALL_FRAME_RBX 11
#define SYSCALL_FRAME_RBP 12
#define SYSCALL_FRAME_RCX 13
#define SYSCALL_FRAME_R11 14
#define SYSCALL_FRAME_QWORDS 15

void syscall_init(void);
uint64_t syscall_get_user_rsp(void);
void syscall_set_user_rsp(uint64_t user_rsp);

uint64_t syscall_dispatch(uint64_t saved_rsp,
                          uint64_t num,
                          uint64_t arg1,
                          uint64_t arg2,
                          uint64_t arg3,
                          uint64_t arg4);
