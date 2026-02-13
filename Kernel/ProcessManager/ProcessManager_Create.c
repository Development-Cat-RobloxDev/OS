#include "ProcessManager.h"
#include "../Memory/Memory_Main.h"
#include "../Serial.h"
#include "../Syscall/Syscall_Main.h"
#include <stddef.h>

#define PROCESS_MAX_COUNT 16
#define PROCESS_STACK_SIZE (16 * 1024)
#define PROCESS_RFLAGS_DEFAULT 0x202ULL
#define PROCESS_STATE_UNUSED 0
#define PROCESS_STATE_READY  1
#define PROCESS_STATE_RUNNING 2
#define PROCESS_STATE_DEAD 3
#define PROCESS_CONTEXT_QWORDS SYSCALL_FRAME_QWORDS

typedef struct {
    uint8_t state;
    uint64_t entry;
    uint64_t saved_rsp;
    uint64_t saved_user_rsp;
    uint8_t *stack_base;
} process_t;

static process_t g_processes[PROCESS_MAX_COUNT];
static int32_t g_current_pid = -1;

static void halt_forever(void) {
    while (1) {
        __asm__ volatile ("hlt");
    }
}

static int32_t find_free_slot(void) {
    for (int32_t i = 0; i < PROCESS_MAX_COUNT; ++i) {
        if (g_processes[i].state == PROCESS_STATE_UNUSED ||
            g_processes[i].state == PROCESS_STATE_DEAD) {
            return i;
        }
    }
    return -1;
}

static int32_t pick_next_ready(int32_t current_pid) {
    if (current_pid < 0) {
        for (int32_t i = 0; i < PROCESS_MAX_COUNT; ++i) {
            if (g_processes[i].state == PROCESS_STATE_READY ||
                g_processes[i].state == PROCESS_STATE_RUNNING) {
                return i;
            }
        }
        return -1;
    }

    for (int32_t step = 1; step <= PROCESS_MAX_COUNT; ++step) {
        int32_t idx = (current_pid + step) % PROCESS_MAX_COUNT;
        if (g_processes[idx].state == PROCESS_STATE_READY) {
            return idx;
        }
    }
    if (g_processes[current_pid].state == PROCESS_STATE_RUNNING ||
        g_processes[current_pid].state == PROCESS_STATE_READY) {
        return current_pid;
    }
    return -1;
}

void process_manager_init(void) {
    for (int32_t i = 0; i < PROCESS_MAX_COUNT; ++i) {
        g_processes[i].state = PROCESS_STATE_UNUSED;
        g_processes[i].entry = 0;
        g_processes[i].saved_rsp = 0;
        g_processes[i].saved_user_rsp = 0;
        g_processes[i].stack_base = NULL;
    }
    g_current_pid = -1;
}

int32_t process_register_boot_process(uint64_t entry, uint64_t user_stack_top) {
    int32_t pid = find_free_slot();
    if (pid < 0) {
        serial_write_string("[OS] [PROC] No free slot for boot process\n");
        return -1;
    }

    g_processes[pid].state = PROCESS_STATE_RUNNING;
    g_processes[pid].entry = entry;
    g_processes[pid].saved_rsp = user_stack_top;
    g_processes[pid].saved_user_rsp = user_stack_top;
    g_processes[pid].stack_base = NULL;
    g_current_pid = pid;

    serial_write_string("[OS] [PROC] Boot process registered\n");
    return pid;
}

int32_t process_create_user(uint64_t entry) {
    if (entry == 0) {
        return -1;
    }

    int32_t pid = find_free_slot();
    if (pid < 0) {
        serial_write_string("[OS] [PROC] No free slot for process create\n");
        return -1;
    }

    uint8_t *stack = (uint8_t *)kmalloc(PROCESS_STACK_SIZE);
    if (stack == NULL) {
        serial_write_string("[OS] [PROC] Stack allocation failed\n");
        return -1;
    }

    uint64_t stack_top = ((uint64_t)(stack + PROCESS_STACK_SIZE)) & ~0xFULL;
    uint64_t *frame = (uint64_t *)(stack_top - (PROCESS_CONTEXT_QWORDS * sizeof(uint64_t)));

    for (uint32_t i = 0; i < PROCESS_CONTEXT_QWORDS; ++i) {
        frame[i] = 0;
    }
    frame[SYSCALL_FRAME_RCX] = entry;
    frame[SYSCALL_FRAME_R11] = PROCESS_RFLAGS_DEFAULT;

    g_processes[pid].state = PROCESS_STATE_READY;
    g_processes[pid].entry = entry;
    g_processes[pid].saved_rsp = (uint64_t)frame;
    g_processes[pid].saved_user_rsp = stack_top;
    g_processes[pid].stack_base = stack;

    return pid;
}

void process_exit_current(void) {
    if (g_current_pid < 0 || g_current_pid >= PROCESS_MAX_COUNT) {
        return;
    }
    g_processes[g_current_pid].state = PROCESS_STATE_DEAD;
}

uint64_t process_schedule_on_syscall(uint64_t current_saved_rsp,
                                     uint64_t current_user_rsp,
                                     int request_switch,
                                     uint64_t *next_user_rsp_out) {
    if (next_user_rsp_out != NULL) {
        *next_user_rsp_out = current_user_rsp;
    }

    if (g_current_pid < 0 || g_current_pid >= PROCESS_MAX_COUNT) {
        serial_write_string("[OS] [PROC] Invalid current PID\n");
        return current_saved_rsp;
    }

    process_t *current = &g_processes[g_current_pid];
    if (current->state == PROCESS_STATE_RUNNING || current->state == PROCESS_STATE_READY) {
        current->saved_rsp = current_saved_rsp;
        current->saved_user_rsp = current_user_rsp;
        if (current->state != PROCESS_STATE_DEAD) {
            current->state = PROCESS_STATE_READY;
        }
    }

    if (!request_switch && current->state != PROCESS_STATE_DEAD) {
        current->state = PROCESS_STATE_RUNNING;
        if (next_user_rsp_out != NULL) {
            *next_user_rsp_out = current->saved_user_rsp;
        }
        return current->saved_rsp;
    }

    int32_t next_pid = pick_next_ready(g_current_pid);
    if (next_pid < 0) {
        serial_write_string("[OS] [PROC] No runnable process. Halting.\n");
        halt_forever();
    }

    g_current_pid = next_pid;
    process_t *next = &g_processes[g_current_pid];
    next->state = PROCESS_STATE_RUNNING;
    if (next_user_rsp_out != NULL) {
        *next_user_rsp_out = next->saved_user_rsp;
    }
    return next->saved_rsp;
}
