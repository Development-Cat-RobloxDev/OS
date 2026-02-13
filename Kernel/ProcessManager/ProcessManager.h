#pragma once

#include <stdint.h>

void process_manager_init(void);
int32_t process_register_boot_process(uint64_t entry, uint64_t user_stack_top);
int32_t process_create_user(uint64_t entry);
void process_exit_current(void);
uint64_t process_schedule_on_syscall(uint64_t current_saved_rsp,
                                     uint64_t current_user_rsp,
                                     int request_switch,
                                     uint64_t *next_user_rsp_out);
