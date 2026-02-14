#pragma once
#include <stdint.h>

typedef struct {
    uint32_t width;
    uint32_t height;
    uint8_t *pixels;
} PNGImage;

int png_load(const char *path, PNGImage *out_image);
void png_free(PNGImage *img);