#pragma once

#include <stdint.h>

typedef void (*timer_callback_t)(uint64_t tick);

void timer_init(uint32_t hz);
void timer_set_callback(timer_callback_t cb);
uint64_t timer_ticks(void);
uint32_t timer_hz(void);
void timer_disable_irq0(void);
