#include "PNG_Decoder.h"
#include "../../../Kernel/Memory/Memory_Main.h"

extern int32_t file_open(const char *path, uint64_t flags);
extern int64_t file_read(int32_t fd, void *buffer, uint64_t len);
extern int32_t file_close(int32_t fd);

#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

static int read_u32_be(const uint8_t *buf) {
    return (buf[0] << 24) | (buf[1] << 16) | (buf[2] << 8) | buf[3];
}

int png_load(const char *path, PNGImage *out_image) {
    uint8_t header[8];
    int32_t fd = file_open(path, 0);
    if (fd < 0) return -1;

    if (file_read(fd, header, 8) != 8) return -1;

    const uint8_t png_sig[8] = {137,80,78,71,13,10,26,10};
    for (int i=0;i<8;i++)
        if (header[i] != png_sig[i]) return -1;

    uint8_t ihdr[25];
    if (file_read(fd, ihdr, 25) != 25) return -1;

    int width = read_u32_be(&ihdr[4]);
    int height = read_u32_be(&ihdr[8]);
    uint8_t bit_depth = ihdr[12];
    uint8_t color_type = ihdr[13];

    if (bit_depth != 8 || color_type != 6) return -1;

    size_t pixel_count = width * height;
    uint8_t *pixels = kmalloc(pixel_count * 4);
    if (!pixels) return -1;

    uint8_t buf[4];
    for (size_t i=0;i<pixel_count;i++) {
        if (file_read(fd, buf, 4) != 4) {
            kfree(pixels);
            return -1;
        }
        pixels[i*4+0] = buf[0]; // R
        pixels[i*4+1] = buf[1]; // G
        pixels[i*4+2] = buf[2]; // B
        pixels[i*4+3] = buf[3]; // A
    }

    file_close(fd);

    out_image->width = width;
    out_image->height = height;
    out_image->pixels = pixels;
    return 0;
}

void png_free(PNGImage *img) {
    if (img->pixels) kfree(img->pixels);
    img->pixels = NULL;
}