#include <stdint.h>
#include "../../Syscalls.h"
#include "PNG_Decoder.h"

extern void* kmalloc(uint32_t size);
extern void kfree(void* ptr);

#define PNG_CHUNK_IHDR 0x49484452u
#define PNG_CHUNK_IDAT 0x49444154u
#define PNG_CHUNK_IEND 0x49454E44u

typedef struct {
    uint32_t width;
    uint32_t height;
    uint32_t idat_total_size;
    uint8_t has_ihdr;
    uint8_t has_idat;
} PNGMeta;

static PNGDecodeStatus g_png_last_status = PNG_DECODE_OK;

static void png_set_status(PNGDecodeStatus status)
{
    g_png_last_status = status;
}

PNGDecodeStatus png_decoder_last_status(void)
{
    return g_png_last_status;
}

const char* png_decode_status_string(PNGDecodeStatus status)
{
    switch (status) {
    case PNG_DECODE_OK:
        return "ok";
    case PNG_DECODE_ERR_NULL_ARGUMENT:
        return "null argument";
    case PNG_DECODE_ERR_BAD_SIGNATURE:
        return "bad png signature";
    case PNG_DECODE_ERR_TRUNCATED_CHUNK:
        return "truncated png chunk";
    case PNG_DECODE_ERR_INVALID_IHDR:
        return "invalid ihdr";
    case PNG_DECODE_ERR_UNSUPPORTED_FORMAT:
        return "unsupported png format";
    case PNG_DECODE_ERR_MISSING_IHDR:
        return "missing ihdr";
    case PNG_DECODE_ERR_MISSING_IDAT:
        return "missing idat";
    case PNG_DECODE_ERR_MISSING_IEND:
        return "missing iend";
    case PNG_DECODE_ERR_SIZE_OVERFLOW:
        return "size overflow";
    case PNG_DECODE_ERR_OOM:
        return "out of memory";
    case PNG_DECODE_ERR_ZLIB_UNSUPPORTED:
        return "unsupported zlib stream";
    case PNG_DECODE_ERR_ZLIB_TRUNCATED:
        return "truncated zlib stream";
    case PNG_DECODE_ERR_ZLIB_LEN_MISMATCH:
        return "zlib len mismatch";
    case PNG_DECODE_ERR_DECOMP_SIZE_MISMATCH:
        return "decompressed size mismatch";
    case PNG_DECODE_ERR_BAD_FILTER:
        return "unsupported png filter";
    default:
        return "unknown decode error";
    }
}

const char* png_decoder_last_status_string(void)
{
    return png_decode_status_string(g_png_last_status);
}

static uint32_t read_be32(const uint8_t* p)
{
    return ((uint32_t)p[0] << 24) |
           ((uint32_t)p[1] << 16) |
           ((uint32_t)p[2] << 8)  |
           ((uint32_t)p[3]);
}

static int add_u32_checked(uint32_t a, uint32_t b, uint32_t* out)
{
    if ((uint32_t)(UINT32_MAX - a) < b) {
        return 0;
    }
    *out = a + b;
    return 1;
}

static int mul_u64_checked(uint64_t a, uint64_t b, uint64_t* out)
{
    if (a != 0 && b > (UINT64_MAX / a)) {
        return 0;
    }
    *out = a * b;
    return 1;
}

static int png_parse_meta(
    const uint8_t* buffer,
    uint64_t size,
    PNGMeta* meta)
{
    const uint8_t* p = buffer + 8;
    const uint8_t* end = buffer + size;

    meta->width = 0;
    meta->height = 0;
    meta->idat_total_size = 0;
    meta->has_ihdr = 0;
    meta->has_idat = 0;

    while (p < end) {
        uint32_t len;
        uint32_t type;

        if ((uint64_t)(end - p) < 8u) {
            png_set_status(PNG_DECODE_ERR_TRUNCATED_CHUNK);
            return 0;
        }

        len = read_be32(p);
        p += 4;
        type = read_be32(p);
        p += 4;

        if ((uint64_t)(end - p) < (uint64_t)len + 4u) {
            png_set_status(PNG_DECODE_ERR_TRUNCATED_CHUNK);
            return 0;
        }

        if (type == PNG_CHUNK_IHDR) {
            if (len < 13u || meta->has_ihdr) {
                png_set_status(PNG_DECODE_ERR_INVALID_IHDR);
                return 0;
            }

            meta->width = read_be32(p);
            meta->height = read_be32(p + 4);

            if (meta->width == 0u || meta->height == 0u) {
                png_set_status(PNG_DECODE_ERR_INVALID_IHDR);
                return 0;
            }

            if (p[8] != 8u || p[9] != 6u || p[10] != 0u ||
                p[11] != 0u || p[12] != 0u) {
                png_set_status(PNG_DECODE_ERR_UNSUPPORTED_FORMAT);
                return 0;
            }

            meta->has_ihdr = 1;
        } else if (type == PNG_CHUNK_IDAT) {
            uint32_t next_total;

            if (!meta->has_ihdr) {
                png_set_status(PNG_DECODE_ERR_INVALID_IHDR);
                return 0;
            }

            if (!add_u32_checked(meta->idat_total_size, len, &next_total)) {
                png_set_status(PNG_DECODE_ERR_SIZE_OVERFLOW);
                return 0;
            }

            meta->idat_total_size = next_total;
            meta->has_idat = 1;
        } else if (type == PNG_CHUNK_IEND) {
            if (!meta->has_ihdr) {
                png_set_status(PNG_DECODE_ERR_MISSING_IHDR);
                return 0;
            }
            if (!meta->has_idat) {
                png_set_status(PNG_DECODE_ERR_MISSING_IDAT);
                return 0;
            }
            return 1;
        }

        p += (uint64_t)len + 4u;
    }

    png_set_status(PNG_DECODE_ERR_MISSING_IEND);
    return 0;
}

static int png_copy_idat(
    const uint8_t* buffer,
    uint64_t size,
    uint8_t* out,
    uint32_t out_size)
{
    const uint8_t* p = buffer + 8;
    const uint8_t* end = buffer + size;
    uint32_t written = 0;

    while (p < end) {
        uint32_t len;
        uint32_t type;

        if ((uint64_t)(end - p) < 8u) {
            png_set_status(PNG_DECODE_ERR_TRUNCATED_CHUNK);
            return 0;
        }

        len = read_be32(p);
        p += 4;
        type = read_be32(p);
        p += 4;

        if ((uint64_t)(end - p) < (uint64_t)len + 4u) {
            png_set_status(PNG_DECODE_ERR_TRUNCATED_CHUNK);
            return 0;
        }

        if (type == PNG_CHUNK_IDAT) {
            if ((uint64_t)written + (uint64_t)len > (uint64_t)out_size) {
                png_set_status(PNG_DECODE_ERR_SIZE_OVERFLOW);
                return 0;
            }
            memcpy(out + written, p, len);
            written += len;
        } else if (type == PNG_CHUNK_IEND) {
            break;
        }

        p += (uint64_t)len + 4u;
    }

    if (written != out_size) {
        png_set_status(PNG_DECODE_ERR_DECOMP_SIZE_MISMATCH);
        return 0;
    }

    return 1;
}

static int png_unfilter(
    uint8_t* data,
    uint32_t data_size,
    uint32_t width,
    uint32_t height)
{
    uint64_t stride64;
    uint64_t scanline64;
    uint64_t required64;
    uint32_t stride;
    uint32_t scanline;
    uint8_t* prev = NULL;
    uint8_t* cur = data;

    if (!mul_u64_checked((uint64_t)width, 4u, &stride64)) {
        png_set_status(PNG_DECODE_ERR_SIZE_OVERFLOW);
        return 0;
    }

    if (stride64 > UINT32_MAX) {
        png_set_status(PNG_DECODE_ERR_SIZE_OVERFLOW);
        return 0;
    }

    scanline64 = stride64 + 1u;
    if (!mul_u64_checked(scanline64, (uint64_t)height, &required64)) {
        png_set_status(PNG_DECODE_ERR_SIZE_OVERFLOW);
        return 0;
    }

    if (required64 != (uint64_t)data_size) {
        png_set_status(PNG_DECODE_ERR_DECOMP_SIZE_MISMATCH);
        return 0;
    }

    stride = (uint32_t)stride64;
    scanline = (uint32_t)scanline64;

    for (uint32_t y = 0; y < height; y++) {
        uint8_t filter = cur[0];
        uint8_t* row = cur + 1;

        switch (filter) {
        case 0:
            break;

        case 1:
            for (uint32_t x = 4; x < stride; x++) {
                row[x] = (uint8_t)(row[x] + row[x - 4]);
            }
            break;

        case 2:
            if (prev) {
                for (uint32_t x = 0; x < stride; x++) {
                    row[x] = (uint8_t)(row[x] + prev[x]);
                }
            }
            break;

        case 3:
            for (uint32_t x = 0; x < stride; x++) {
                uint8_t left = (x >= 4) ? row[x - 4] : 0;
                uint8_t up = prev ? prev[x] : 0;
                row[x] = (uint8_t)(row[x] + ((left + up) / 2));
            }
            break;

        case 4:
            for (uint32_t x = 0; x < stride; x++) {
                uint8_t a = (x >= 4) ? row[x - 4] : 0;
                uint8_t b = prev ? prev[x] : 0;
                uint8_t c = (x >= 4 && prev) ? prev[x - 4] : 0;
                int p = (int)a + (int)b - (int)c;
                int pa = (p > (int)a) ? (p - (int)a) : ((int)a - p);
                int pb = (p > (int)b) ? (p - (int)b) : ((int)b - p);
                int pc = (p > (int)c) ? (p - (int)c) : ((int)c - p);
                uint8_t pr = (pa <= pb && pa <= pc) ? a :
                             ((pb <= pc) ? b : c);
                row[x] = (uint8_t)(row[x] + pr);
            }
            break;

        default:
            png_set_status(PNG_DECODE_ERR_BAD_FILTER);
            return 0;
        }

        prev = row;
        cur += scanline;
    }

    return 1;
}

static uint8_t* zlib_decompress_uncompressed(
    const uint8_t* data,
    uint32_t size,
    uint32_t* out_size)
{
    uint32_t pos = 0;
    uint8_t cmf;
    uint8_t flg;
    uint8_t header;
    uint16_t len;
    uint16_t nlen;
    uint8_t* out;

    if (!data || !out_size) {
        png_set_status(PNG_DECODE_ERR_NULL_ARGUMENT);
        return NULL;
    }

    *out_size = 0;

    if (size < 2u) {
        png_set_status(PNG_DECODE_ERR_ZLIB_TRUNCATED);
        return NULL;
    }

    cmf = data[pos++];
    flg = data[pos++];

    if ((cmf & 0x0Fu) != 8u || ((((uint32_t)cmf << 8) | (uint32_t)flg) % 31u) != 0u ||
        (flg & 0x20u) != 0u) {
        png_set_status(PNG_DECODE_ERR_ZLIB_UNSUPPORTED);
        return NULL;
    }

    if (pos >= size) {
        png_set_status(PNG_DECODE_ERR_ZLIB_TRUNCATED);
        return NULL;
    }

    header = data[pos++];

    if (((header >> 1) & 0x03u) != 0u) {
        png_set_status(PNG_DECODE_ERR_ZLIB_UNSUPPORTED);
        return NULL;
    }

    if ((header & 0x01u) == 0u) {
        png_set_status(PNG_DECODE_ERR_ZLIB_UNSUPPORTED);
        return NULL;
    }

    if ((uint64_t)size - (uint64_t)pos < 4u) {
        png_set_status(PNG_DECODE_ERR_ZLIB_TRUNCATED);
        return NULL;
    }

    len = (uint16_t)((uint16_t)data[pos] | ((uint16_t)data[pos + 1] << 8));
    nlen = (uint16_t)((uint16_t)data[pos + 2] | ((uint16_t)data[pos + 3] << 8));
    pos += 4;

    if ((uint16_t)(len ^ 0xFFFFu) != nlen) {
        png_set_status(PNG_DECODE_ERR_ZLIB_LEN_MISMATCH);
        return NULL;
    }

    if (len == 0u || (uint64_t)size - (uint64_t)pos < (uint64_t)len + 4u) {
        png_set_status(PNG_DECODE_ERR_ZLIB_TRUNCATED);
        return NULL;
    }

    out = kmalloc((uint32_t)len);
    if (!out) {
        png_set_status(PNG_DECODE_ERR_OOM);
        return NULL;
    }

    memcpy(out, data + pos, len);
    *out_size = (uint32_t)len;
    return out;
}

uint32_t* png_decode_buffer(
    const uint8_t* buffer,
    uint64_t size,
    uint32_t* out_w,
    uint32_t* out_h)
{
    static const uint8_t sig[8] = {137, 80, 78, 71, 13, 10, 26, 10};
    PNGMeta meta;
    uint8_t* idat_data = NULL;
    uint8_t* decomp = NULL;
    uint32_t* out = NULL;
    uint32_t decomp_size = 0;
    uint64_t stride64 = 0;
    uint64_t scanline64 = 0;
    uint64_t expected_decomp64 = 0;
    uint64_t pixel_count64 = 0;
    uint64_t out_bytes64 = 0;

    if (out_w) {
        *out_w = 0;
    }
    if (out_h) {
        *out_h = 0;
    }

    png_set_status(PNG_DECODE_OK);

    if (!buffer || !out_w || !out_h) {
        png_set_status(PNG_DECODE_ERR_NULL_ARGUMENT);
        return NULL;
    }

    if (size < 8u || memcmp(buffer, sig, 8u) != 0) {
        png_set_status(PNG_DECODE_ERR_BAD_SIGNATURE);
        return NULL;
    }

    if (!png_parse_meta(buffer, size, &meta)) {
        return NULL;
    }

    if (meta.idat_total_size == 0u) {
        png_set_status(PNG_DECODE_ERR_MISSING_IDAT);
        return NULL;
    }

    if (!mul_u64_checked((uint64_t)meta.width, 4u, &stride64)) {
        png_set_status(PNG_DECODE_ERR_SIZE_OVERFLOW);
        return NULL;
    }

    scanline64 = stride64 + 1u;
    if (!mul_u64_checked(scanline64, (uint64_t)meta.height, &expected_decomp64)) {
        png_set_status(PNG_DECODE_ERR_SIZE_OVERFLOW);
        return NULL;
    }

    if (expected_decomp64 == 0u || expected_decomp64 > UINT32_MAX) {
        png_set_status(PNG_DECODE_ERR_SIZE_OVERFLOW);
        return NULL;
    }

    if (!mul_u64_checked((uint64_t)meta.width, (uint64_t)meta.height, &pixel_count64) ||
        !mul_u64_checked(pixel_count64, 4u, &out_bytes64) ||
        out_bytes64 == 0u || out_bytes64 > UINT32_MAX) {
        png_set_status(PNG_DECODE_ERR_SIZE_OVERFLOW);
        return NULL;
    }

    idat_data = kmalloc(meta.idat_total_size);
    if (!idat_data) {
        png_set_status(PNG_DECODE_ERR_OOM);
        return NULL;
    }

    if (!png_copy_idat(buffer, size, idat_data, meta.idat_total_size)) {
        kfree(idat_data);
        return NULL;
    }

    decomp = zlib_decompress_uncompressed(idat_data, meta.idat_total_size, &decomp_size);
    kfree(idat_data);

    if (!decomp) {
        return NULL;
    }

    if ((uint64_t)decomp_size != expected_decomp64) {
        png_set_status(PNG_DECODE_ERR_DECOMP_SIZE_MISMATCH);
        kfree(decomp);
        return NULL;
    }

    if (!png_unfilter(decomp, decomp_size, meta.width, meta.height)) {
        kfree(decomp);
        return NULL;
    }

    out = kmalloc((uint32_t)out_bytes64);
    if (!out) {
        png_set_status(PNG_DECODE_ERR_OOM);
        kfree(decomp);
        return NULL;
    }

    {
        uint8_t* src = decomp;
        for (uint32_t y = 0; y < meta.height; y++) {
            uint64_t dst_index = (uint64_t)y * (uint64_t)meta.width;
            memcpy((uint8_t*)&out[dst_index], src + 1, (size_t)stride64);
            src += (size_t)scanline64;
        }
    }

    kfree(decomp);

    *out_w = meta.width;
    *out_h = meta.height;
    png_set_status(PNG_DECODE_OK);
    return out;
}
