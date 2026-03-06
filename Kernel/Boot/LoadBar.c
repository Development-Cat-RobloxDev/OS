#include "LoadBar.h"

#include "../Drivers/Display/Display_Main.h"

#define SPINNER_DOTS         12u
#define SPINNER_REF_RADIUS   64u
#define SPINNER_RADIUS_MIN   18u
#define SPINNER_RADIUS_MAX   42u
#define SPINNER_DOT_SIZE     3u
#define SPINNER_MARGIN_PX    32u
#define SPINNER_UPDATE_TICKS 10u

#define COLOR_BG       0x000000u
#define COLOR_PRIMARY  0x2994FFu

static uint32_t g_center_x = 0;
static uint32_t g_center_y = 0;
static uint32_t g_radius   = 0;
static uint32_t g_box_x = 0;
static uint32_t g_box_y = 0;
static uint32_t g_box_w = 0;
static uint32_t g_box_h = 0;

static volatile uint8_t  g_ready = 0;
static volatile uint32_t g_phase = 0;
static volatile uint32_t g_tick_accum = 0;

static const int8_t k_dx[SPINNER_DOTS] = {
     0, 12, 21, 24, 21, 12,  0, -12, -21, -24, -21, -12
};
static const int8_t k_dy[SPINNER_DOTS] = {
    -24, -21, -12, 0, 12, 21, 24, 21, 12, 0, -12, -21
};

static uint32_t scale_color(uint32_t color, uint8_t brightness) {
    uint32_t r = (color >> 16) & 0xFFu;
    uint32_t g = (color >> 8)  & 0xFFu;
    uint32_t b =  color        & 0xFFu;

    r = (r * brightness) / 255u;
    g = (g * brightness) / 255u;
    b = (b * brightness) / 255u;

    return (r << 16) | (g << 8) | b;
}

static void spinner_clear_box(void) {
    display_fill_rect(g_box_x, g_box_y, g_box_w, g_box_h, COLOR_BG);
}

static void spinner_draw(uint32_t phase) {
    if (!g_ready) return;

    spinner_clear_box();

    for (uint32_t i = 0; i < SPINNER_DOTS; ++i) {
        uint32_t rank = (phase + i) % SPINNER_DOTS;
        uint8_t brightness = (uint8_t)(80u + (uint32_t)(175u * (SPINNER_DOTS - rank) / SPINNER_DOTS));
        uint32_t color = scale_color(COLOR_PRIMARY, brightness);

        int32_t dx = ((int32_t)k_dx[i] * (int32_t)g_radius) / (int32_t)SPINNER_REF_RADIUS;
        int32_t dy = ((int32_t)k_dy[i] * (int32_t)g_radius) / (int32_t)SPINNER_REF_RADIUS;

        int32_t px = (int32_t)g_center_x + dx - (int32_t)(SPINNER_DOT_SIZE / 2u);
        int32_t py = (int32_t)g_center_y + dy - (int32_t)(SPINNER_DOT_SIZE / 2u);

        display_fill_rect(
            (uint32_t)px,
            (uint32_t)py,
            SPINNER_DOT_SIZE,
            SPINNER_DOT_SIZE,
            color
        );
    }

    display_present();
}

void load_bar_init(void) {
    if (!display_is_ready()) {
        return;
    }

    uint32_t screen_w = display_width();
    uint32_t screen_h = display_height();
    if (screen_w == 0u || screen_h == 0u) {
        return;
    }

    uint32_t max_r = (screen_w < screen_h ? screen_w : screen_h) / 8u;
    if (max_r < SPINNER_RADIUS_MIN) max_r = SPINNER_RADIUS_MIN;
    if (max_r > SPINNER_RADIUS_MAX) max_r = SPINNER_RADIUS_MAX;

    g_radius = max_r;
    g_center_x = screen_w / 2u;
    g_center_y = (screen_h * 3u) / 4u;
    if (g_center_y + g_radius + SPINNER_MARGIN_PX > screen_h) {
        g_center_y = screen_h - g_radius - SPINNER_MARGIN_PX;
    }

    uint32_t diameter = g_radius * 2u + SPINNER_DOT_SIZE + 4u;
    g_box_w = diameter;
    g_box_h = diameter;
    g_box_x = g_center_x - (diameter / 2u);
    g_box_y = g_center_y - (diameter / 2u);

    g_phase = 0;
    g_tick_accum = 0;
    g_ready = 1;

    spinner_clear_box();
    spinner_draw(g_phase);
}

void load_bar_set_target(uint32_t percent) {
    (void)percent;
}

void load_bar_tick(uint64_t tick) {
    (void)tick;
    if (!g_ready) return;

    g_tick_accum++;
    if (g_tick_accum < SPINNER_UPDATE_TICKS) {
        return;
    }
    g_tick_accum = 0;

    g_phase = (g_phase + 1u) % SPINNER_DOTS;
    spinner_draw(g_phase);
}

void load_bar_finish(void) {
    if (!g_ready) return;
    spinner_clear_box();
    display_present();
    g_ready = 0;
}
