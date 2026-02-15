#include <stdint.h>
#include <string.h>
#include "../PNG_Decoder/PNG_Decoder.h"
#include "../../Syscalls.h"

#define SYSCALL_SERIAL_PUTS       2ULL
#define SYSCALL_PROCESS_YIELD     4ULL
#define SYSCALL_DRAW_FILL_RECT    11ULL
#define SYSCALL_DRAW_PRESENT      12ULL

typedef struct {
    uint32_t width;
    uint32_t height;
} ImageSize;

static uint32_t png_rgba_to_argb(uint32_t packed_abgr)
{
    uint32_t a = (packed_abgr >> 24) & 0xFFu;
    uint32_t b = (packed_abgr >> 16) & 0xFFu;
    uint32_t g = (packed_abgr >> 8) & 0xFFu;
    uint32_t r = packed_abgr & 0xFFu;
    return (a << 24) | (r << 16) | (g << 8) | b;
}

static void draw_png_image(const uint32_t* rgba,
                           const ImageSize* img,
                           uint32_t dst_x,
                           uint32_t dst_y)
{
    if (!rgba || !img || img->width == 0 || img->height == 0) {
        return;
    }

    for (uint32_t y = 0; y < img->height; y++) {
        for (uint32_t x = 0; x < img->width; x++) {
            uint32_t argb = png_rgba_to_argb(rgba[y * img->width + x]);
            uint32_t alpha = (argb >> 24) & 0xFFu;
            if (alpha == 0u) {
                continue;
            }
            draw_fill_rect(dst_x + x, dst_y + y, 1, 1, argb);
        }
    }
}

static uint32_t* load_png(const char* path, ImageSize* out_size) {
    static const uint64_t kMaxPngBytes = 1024u * 1024u;
    int32_t fd = file_open(path, 0);
    if (fd < 0) {
        serial_write_string("[U] Failed to open PNG file\n");
        out_size->width = out_size->height = 0;
        return NULL;
    }

    uint8_t* file_buffer = kmalloc((uint32_t)kMaxPngBytes);
    if (!file_buffer) {
        serial_write_string("[U] Failed to allocate memory for PNG\n");
        file_close(fd);
        out_size->width = out_size->height = 0;
        return NULL;
    }

    uint64_t offset = 0;
    int64_t read_bytes = 0;
    while (offset < kMaxPngBytes) {
        uint64_t remain = kMaxPngBytes - offset;
        uint64_t chunk = (remain < 256u) ? remain : 256u;
        read_bytes = file_read(fd, file_buffer + offset, chunk);
        if (read_bytes <= 0) {
            break;
        }
        offset += (uint64_t)read_bytes;
    }

    if (read_bytes < 0) {
        serial_write_string("[U] Failed to read PNG file\n");
        file_close(fd);
        kfree(file_buffer);
        out_size->width = out_size->height = 0;
        return NULL;
    }

    if (offset == kMaxPngBytes) {
        uint8_t sentinel = 0;
        int64_t extra = file_read(fd, &sentinel, 1);
        if (extra < 0) {
            serial_write_string("[U] Failed while checking PNG size\n");
            file_close(fd);
            kfree(file_buffer);
            out_size->width = out_size->height = 0;
            return NULL;
        }
        if (extra > 0) {
            serial_write_string("[U] PNG too large\n");
            file_close(fd);
            kfree(file_buffer);
            out_size->width = out_size->height = 0;
            return NULL;
        }
    }

    file_close(fd);
    serial_write_string("[U] success load PNG\n");

    uint32_t width = 0, height = 0;
    uint32_t* rgba = png_decode_buffer(file_buffer, offset, &width, &height);
    kfree(file_buffer);

    if (!rgba || width == 0 || height == 0) {
        serial_write_string("[U] Failed to decode PNG\n");
        serial_write_string("[U] Decode status: ");
        serial_write_string(png_decoder_last_status_string());
        serial_write_string("\n");
        out_size->width = out_size->height = 0;
        return NULL;
    }

    out_size->width = width;
    out_size->height = height;
    serial_write_string("[U] success decode PNG\n");
    return rgba;
}

void _start(void)
{
    static const uint32_t colors[] = {
        0xFF202020u,
        0xFF204080u,
        0xFF406020u,
        0xFF602040u,
    };
    uint32_t frame = 0;

    serial_write_string("[U][APP] standalone process started\n");
    ImageSize img = {0, 0};
    uint32_t* rgba = load_png("LOGO.PNG", &img);
    if (!rgba) {
        serial_write_string("[U] PNG draw disabled\n");
    }

    while (1) {
        uint32_t bg = colors[frame % (sizeof(colors) / sizeof(colors[0]))];
        draw_fill_rect(0, 0, 640, 480, bg);
        draw_fill_rect(80, 80, 200, 120, 0xFFFFFFFFu);
        draw_fill_rect(100, 100, 160, 80, 0xFF101010u);
        draw_png_image(rgba, &img, 16, 16);
        draw_present();

        ++frame;
        process_yield();
    }
}
