#include "Display_Main.h"
#include "VirtIO/VirtIO.h"

bool display_init(void) {
    return virtio_gpu_init();
}

bool display_is_ready(void) {
    return virtio_gpu_is_ready();
}

uint32_t display_width(void) {
    return virtio_gpu_width();
}

uint32_t display_height(void) {
    return virtio_gpu_height();
}

void display_draw_pixel(uint32_t x, uint32_t y, uint32_t color) {
    virtio_gpu_draw_pixel(x, y, color);
}

void display_fill_rect(uint32_t x, uint32_t y, uint32_t w, uint32_t h, uint32_t color) {
    virtio_gpu_fill_rect(x, y, w, h, color);
}

void display_present(void) {
    virtio_gpu_present();
}
