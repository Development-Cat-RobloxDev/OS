#pragma pack(push, 1)
#include <stdbool.h>
#include <stdint.h>

typedef struct __attribute__((packed)) {
    uint16_t bfType;
    uint32_t bfSize;
    uint16_t bfReserved1;
    uint16_t bfReserved2;
    uint32_t bfOffBits;
} BMPFileHeader;

typedef struct __attribute__((packed)) {
    uint32_t biSize;
    int32_t  biWidth;
    int32_t  biHeight;
    uint16_t biPlanes;
    uint16_t biBitCount;
    uint32_t biCompression;
    uint32_t biSizeImage;
    int32_t  biXPelsPerMeter;
    int32_t  biYPelsPerMeter;
    uint32_t biClrUsed;
    uint32_t biClrImportant;
} BMPInfoHeader;

bool load_bmp(const char* path, void** out_buffer, uint32_t* out_size);
void draw_bmp_center(void* bmp_data);
void draw_bmp_center_ex(void* bmp_data, uint32_t bg_color);
#pragma pack(pop)