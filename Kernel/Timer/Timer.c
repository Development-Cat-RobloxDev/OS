#include "Timer.h"
#include <stddef.h>
#include <stdint.h>

#include "../IDT/IDT_Main.h"
#include "../IO/IO_Main.h"
#include "../Serial.h"

#define PIT_CHANNEL0_DATA 0x40
#define PIT_COMMAND       0x43
#define PIT_BASE_FREQ     1193182U
#define IRQ_VECTOR_TIMER  32u

static volatile uint64_t g_tick_count = 0;
static uint32_t g_timer_hz = 0;
static timer_callback_t g_tick_callback = NULL;
static int g_timer_initialized = 0;

static void pit_set_frequency(uint32_t hz) {
    if (hz == 0) return;

    uint32_t clamped_hz = hz;
    if (clamped_hz < 10u)  clamped_hz = 10u;
    if (clamped_hz > 1000u) clamped_hz = 1000u;

    uint32_t divisor32 = PIT_BASE_FREQ / clamped_hz;
    if (divisor32 == 0u) divisor32 = 1u;
    if (divisor32 > 0xFFFFu) divisor32 = 0xFFFFu;

    uint16_t divisor = (uint16_t)divisor32;

    outb(PIT_COMMAND, 0x36);
    outb(PIT_CHANNEL0_DATA, (uint8_t)(divisor & 0xFF));
    outb(PIT_CHANNEL0_DATA, (uint8_t)((divisor >> 8) & 0xFF));

    g_timer_hz = clamped_hz;
}

static void timer_irq_handler(void) {
    g_tick_count++;
    timer_callback_t cb = g_tick_callback;
    if (cb) {
        cb(g_tick_count);
    }
}

void timer_init(uint32_t hz) {
    if (g_timer_initialized) {
        return;
    }

    register_interrupt_handler(IRQ_VECTOR_TIMER, timer_irq_handler);

    pit_set_frequency(hz);
    
    uint8_t master_mask = inb(0x21);
    master_mask &= (uint8_t)~0x01u;
    outb(0x21, master_mask);

    g_timer_initialized = 1;
    serial_write_string("[OS] [TIMER] PIT initialized\n");
}

void timer_set_callback(timer_callback_t cb) {
    g_tick_callback = cb;
}

uint64_t timer_ticks(void) {
    return g_tick_count;
}

uint32_t timer_hz(void) {
    return g_timer_hz;
}

void timer_disable_irq0(void) {
    uint8_t master_mask = inb(0x21);
    master_mask |= 0x01u;
    outb(0x21, master_mask);
}
