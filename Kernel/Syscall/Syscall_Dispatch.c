#include "Syscall_Main.h"
#include "Syscall_File.h"
#include "IO/IO_Main.h"
#include "../Serial.h"
#include "../ProcessManager/ProcessManager.h"
#include "../Drivers/Display/Display_Main.h"
#include "../Memory/Memory_Main.h"
#include "../Memory/Other_Utils.h"
#include <stdint.h>

static void set_syscall_result(uint64_t saved_rsp, uint64_t value)
{
    uint64_t *frame = (uint64_t *)saved_rsp;
    frame[SYSCALL_FRAME_RAX] = value;
}

uint64_t syscall_dispatch(uint64_t saved_rsp,
                          uint64_t num,
                          uint64_t arg1,
                          uint64_t arg2,
                          uint64_t arg3,
                          uint64_t arg4)
{
    int request_switch = 0;

    switch (num) {
    case SYSCALL_SERIAL_PUTCHAR:
        serial_write_char((char)arg1);
        set_syscall_result(saved_rsp, 0);
        break;

    case SYSCALL_SERIAL_PUTS:
        serial_write_string((const char*)arg1);
        set_syscall_result(saved_rsp, 0);
        break;

    case SYSCALL_PROCESS_CREATE: {
        int32_t pid = process_create_user(arg1);
        set_syscall_result(saved_rsp, (uint64_t)(int64_t)pid);
        break;
    }

    case SYSCALL_PROCESS_SPAWN_ELF: {
        int32_t pid = process_spawn_user_elf((const char *)arg1);
        set_syscall_result(saved_rsp, (uint64_t)(int64_t)pid);
        break;
    }

    case SYSCALL_PROCESS_YIELD:
        set_syscall_result(saved_rsp, 0);
        request_switch = 1;
        break;

    case SYSCALL_PROCESS_EXIT:
        process_exit_current();
        request_switch = 1;
        break;

    case SYSCALL_THREAD_CREATE: {
        int32_t tid = process_create_user(arg1);
        set_syscall_result(saved_rsp, (uint64_t)(int64_t)tid);
        if (tid >= 0) {
            request_switch = 1;
        }
        break;
    }

    case SYSCALL_DRAW_PIXEL:
        display_draw_pixel((uint32_t)arg1, (uint32_t)arg2, (uint32_t)arg3);
        set_syscall_result(saved_rsp, 0);
        break;

    case SYSCALL_DRAW_FILL_RECT:
        display_fill_rect((uint32_t)arg1,
                          (uint32_t)arg2,
                          (uint32_t)(arg3 >> 32),
                          (uint32_t)(arg3 & 0xFFFFFFFFu),
                          (uint32_t)arg4);
        set_syscall_result(saved_rsp, 0);
        break;

    case SYSCALL_DRAW_PRESENT:
        display_present();
        set_syscall_result(saved_rsp, 0);
        break;

    case SYSCALL_FILE_OPEN: {
        int32_t fd = syscall_file_open((const char *)arg1, arg2);
        set_syscall_result(saved_rsp, (uint64_t)(int64_t)fd);
        break;
    }

    case SYSCALL_FILE_READ: {
        int64_t n = syscall_file_read((int32_t)arg1, (uint8_t *)arg2, arg3);
        set_syscall_result(saved_rsp, (uint64_t)n);
        break;
    }

    case SYSCALL_FILE_WRITE: {
        int64_t n = syscall_file_write((int32_t)arg1, (const uint8_t *)arg2, arg3);
        set_syscall_result(saved_rsp, (uint64_t)n);
        break;
    }

    case SYSCALL_FILE_CLOSE: {
        int32_t rc = syscall_file_close((int32_t)arg1);
        set_syscall_result(saved_rsp, (uint64_t)(int64_t)rc);
        break;
    }

    case SYSCALL_USER_KMALLOC: {
        uint32_t size = (uint32_t)arg1;
        void *ptr = kmalloc(size);
        set_syscall_result(saved_rsp, (uint64_t)ptr);
        break;
    }

    case SYSCALL_USER_KFREE: {
        void *ptr = (void*)arg1;
        kfree(ptr);
        set_syscall_result(saved_rsp, 0);
        break;
    }

        case SYSCALL_USER_MEMCPY: {
        void *dst = (void*)arg1;
        const void *src = (const void*)arg2;
        uint64_t n = arg3;

        uint8_t *d = (uint8_t*)dst;
        const uint8_t *s = (const uint8_t*)src;

        for (uint64_t i = 0; i < n; i++) {
            d[i] = s[i];
        }

        set_syscall_result(saved_rsp, (uint64_t)dst);
        break;
    }

    case SYSCALL_USER_MEMCMP: {
        const uint8_t *s1 = (const uint8_t*)arg1;
        const uint8_t *s2 = (const uint8_t*)arg2;
        uint64_t n = arg3;

        int result = 0;

        for (uint64_t i = 0; i < n; i++) {
            if (s1[i] != s2[i]) {
                result = (int)s1[i] - (int)s2[i];
                break;
            }
        }

        set_syscall_result(saved_rsp, (uint64_t)(int64_t)result);
        break;
    }

    default:
        serial_write_string("[SYSCALL] Unknown syscall\n");
        set_syscall_result(saved_rsp, (uint64_t)-1);
        break;
    }

    uint64_t current_user_rsp = syscall_get_user_rsp();
    uint64_t next_user_rsp = current_user_rsp;
    uint64_t next_saved_rsp = process_schedule_on_syscall(saved_rsp,
                                                          current_user_rsp,
                                                          request_switch,
                                                          &next_user_rsp);
    syscall_set_user_rsp(next_user_rsp);
    return next_saved_rsp;
}
