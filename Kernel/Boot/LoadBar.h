#pragma once

#include <stdint.h>

void load_bar_init(void);
void load_bar_set_target(uint32_t percent);
void load_bar_tick(uint64_t tick);
void load_bar_finish(void);
