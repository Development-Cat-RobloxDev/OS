#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "BMP.h"
#include "DefaultLibrary/DefaultLibrary.h"
#include "Drivers/Display/Display_Driver.h"
#include "Drivers/FileSystem/FAT32/FAT32_Main.h"
#include "Serial.h"

static uint32_t align4(uint32_t x) {
    return (x + 3) & ~3;
}

bool load_bmp(const char* path, void** out_buffer, uint32_t* out_size) {
    FAT32_FILE file;
    if (!fat32_find_file(path, &file)) {
        serial_write_string("BMP: file not found\n");
        return false;
    }

    uint32_t size = fat32_get_file_size(&file);
    serial_write_string("BMP size = ");
    serial_write_uint32(size);
    serial_write_string("\n");
    if (size == 0) return false;

    void* buffer = malloc(size);
    if (!buffer) return false;

    serial_write_string("BMP: reading file\n");

    if (!fat32_read_file(&file, buffer)) {
        free(buffer);
        return false;
    }
    serial_write_string("BMP: successfully reading file\n");

    *out_buffer = buffer;
    *out_size   = size;
    return true;
}

static uint32_t convert_color(uint8_t r, uint8_t g, uint8_t b) {
    return ((uint32_t)r << 16) | ((uint32_t)g << 8) | b;
}

static uint32_t alpha_blend(uint32_t fg, uint8_t alpha, uint32_t bg) {
    if (alpha == 255) return fg;
    if (alpha == 0)   return bg;

    uint8_t fg_r = (fg >> 16) & 0xFF;
    uint8_t fg_g = (fg >>  8) & 0xFF;
    uint8_t fg_b =  fg        & 0xFF;

    uint8_t bg_r = (bg >> 16) & 0xFF;
    uint8_t bg_g = (bg >>  8) & 0xFF;
    uint8_t bg_b =  bg        & 0xFF;

    uint8_t r = (uint8_t)(((uint16_t)fg_r * alpha + (uint16_t)bg_r * (255 - alpha)) / 255);
    uint8_t g = (uint8_t)(((uint16_t)fg_g * alpha + (uint16_t)bg_g * (255 - alpha)) / 255);
    uint8_t b = (uint8_t)(((uint16_t)fg_b * alpha + (uint16_t)bg_b * (255 - alpha)) / 255);

    return convert_color(r, g, b);
}

static bool decode_rle8(const uint8_t* src, uint32_t src_size,
                         uint8_t* dst, int32_t width, int32_t height) {
    int32_t  abs_height = height > 0 ? height : -height;
    uint32_t dst_size   = (uint32_t)(width * abs_height);

    uint32_t si = 0;
    int32_t  x  = 0;
    int32_t  y  = 0;

    while (si + 1 < src_size) {
        uint8_t b0 = src[si++];
        uint8_t b1 = src[si++];

        if (b0 != 0x00) {
            for (uint8_t i = 0; i < b0; i++) {
                if (x >= width) { x = 0; y++; }
                uint32_t di = (uint32_t)(y * width + x);
                if (di < dst_size) dst[di] = b1;
                x++;
            }
        } else {
            if (b1 == 0x00) {
                x = 0; y++;
            } else if (b1 == 0x01) {
                return true;
            } else if (b1 == 0x02) {
                if (si + 1 >= src_size) return false;
                x += src[si++];
                y += src[si++];
            } else {
                for (uint8_t i = 0; i < b1; i++) {
                    if (si >= src_size) return false;
                    if (x >= width) { x = 0; y++; }
                    uint32_t di = (uint32_t)(y * width + x);
                    if (di < dst_size) dst[di] = src[si];
                    si++; x++;
                }
                if (b1 & 1) si++;
            }
        }
    }
    return true;
}

static bool palette_has_alpha(const uint8_t* pal_raw, int color_count) {
    for (int i = 0; i < color_count; i++) {
        if (pal_raw[i * 4 + 3] != 0x00) return true;
    }
    return false;
}

void draw_bmp_center(void* bmp_data) {
    draw_bmp_center_ex(bmp_data, 0x000000);
}

void draw_bmp_center_ex(void* bmp_data, uint32_t bg_color) {
    if (!bmp_data) return;

    BMPFileHeader* file = (BMPFileHeader*)bmp_data;
    if (file->bfType != 0x4D42) return;

    BMPInfoHeader* info = (BMPInfoHeader*)((uint8_t*)bmp_data + sizeof(BMPFileHeader));

    int32_t  width       = info->biWidth;
    int32_t  height      = info->biHeight;
    int32_t  abs_height  = height > 0 ? height : -height;
    uint16_t bpp         = info->biBitCount;
    uint32_t compression = info->biCompression;

    if (compression == 1 && bpp != 8) return;
    if (compression == 3 && bpp != 32) {
        serial_write_string("[BMP] BI_BITFIELDS is only supported for 32bpp\n");
        return;
    }
    if (compression != 0 && compression != 1 && compression != 3) {
        serial_write_string("[BMP] Unsupported compression\n");
        return;
    }

    uint8_t* pixel_data = (uint8_t*)bmp_data + file->bfOffBits;

    int screen_w = display_width();
    int screen_h = display_height();
    int start_x  = (screen_w - width)      / 2;
    int start_y  = (screen_h - abs_height) / 2;

    uint8_t* pal_raw     = NULL;
    bool     pal_alpha   = false;
    int      color_count = 0;

    if (bpp == 8) {
        pal_raw      = (uint8_t*)((uint8_t*)bmp_data + sizeof(BMPFileHeader) + info->biSize);
        color_count  = (info->biClrUsed != 0) ? (int)info->biClrUsed : 256;
        pal_alpha    = palette_has_alpha(pal_raw, color_count);
    }

    uint8_t* rle_buf = NULL;
    if (compression == 1) {
        uint32_t buf_size = (uint32_t)(width * abs_height);
        rle_buf = (uint8_t*)malloc(buf_size);
        if (!rle_buf) {
            serial_write_string("[BMP] RLE buffer malloc failed\n");
            return;
        }
        memset(rle_buf, 0, buf_size);

        uint32_t src_size = file->bfSize - file->bfOffBits;
        if (!decode_rle8(pixel_data, src_size, rle_buf, width, abs_height)) {
            serial_write_string("[BMP] RLE8 decode failed\n");
            free(rle_buf);
            return;
        }
    }

    uint32_t row_size = 0;
    if (compression == 0 || compression == 3) {
        if      (bpp == 24) row_size = align4((uint32_t)width * 3);
        else if (bpp == 32) row_size = (uint32_t)width * 4;
        else if (bpp == 8)  row_size = align4((uint32_t)width);
        else { return; }
    }

    for (int y = 0; y < abs_height; y++) {
        int bmp_y = (height > 0) ? (abs_height - 1 - y) : y;

        for (int x = 0; x < width; x++) {
            int px = start_x + x;
            int py = start_y + y;
            if (px < 0 || px >= screen_w || py < 0 || py >= screen_h) continue;

            uint32_t color = 0;
            uint8_t  alpha = 255;

            if (compression == 1) {
                uint8_t  idx = rle_buf[(uint32_t)(bmp_y * width + x)];
                uint8_t* e   = pal_raw + (int)idx * 4;
                color = convert_color(e[2], e[1], e[0]);
                alpha = pal_alpha ? e[3] : 255;
            } else {
                uint8_t* row = pixel_data + row_size * (uint32_t)bmp_y;

                if (bpp == 24) {
                    uint8_t* p = row + x * 3;
                    color = convert_color(p[2], p[1], p[0]);
                } else if (bpp == 32) {
                    uint8_t* p = row + x * 4;
                    color = convert_color(p[2], p[1], p[0]);
                    alpha = p[3];
                } else if (bpp == 8) {
                    uint8_t  idx = row[x];
                    uint8_t* e   = pal_raw + (int)idx * 4;
                    color = convert_color(e[2], e[1], e[0]);
                    alpha = pal_alpha ? e[3] : 255;
                }
            }

            if (alpha == 0) continue;
            if (alpha < 255) color = alpha_blend(color, alpha, bg_color);

            display_draw_pixel((uint32_t)px, (uint32_t)py, color);
        }
    }

    display_present();

    if (rle_buf) free(rle_buf);
}