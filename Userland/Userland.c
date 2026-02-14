#include <stdint.h>
#include "../Kernel/Memory/Other_Utils.h"

#define SYSCALL_SERIAL_PUTCHAR  1ULL
#define SYSCALL_SERIAL_PUTS     2ULL
#define SYSCALL_PROCESS_CREATE  3ULL
#define SYSCALL_PROCESS_YIELD   4ULL
#define SYSCALL_PROCESS_EXIT    5ULL
#define SYSCALL_THREAD_CREATE   6ULL
#define SYSCALL_DRAW_PIXEL      10ULL
#define SYSCALL_DRAW_FILL_RECT  11ULL
#define SYSCALL_DRAW_PRESENT    12ULL
#define SYSCALL_FILE_OPEN       20ULL
#define SYSCALL_FILE_READ       21ULL
#define SYSCALL_FILE_WRITE      22ULL
#define SYSCALL_FILE_CLOSE      23ULL
#define SYSCALL_USER_KMALLOC    24ULL
#define SYSCALL_USER_KFREE      25ULL

static inline uint64_t syscall0(uint64_t num)
{
    uint64_t ret;
    __asm__ volatile (
        "syscall"
        : "=a"(ret)
        : "a"(num)
        : "rcx", "r11", "memory"
    );
    return ret;
}

static inline uint64_t syscall1(uint64_t num, uint64_t arg1)
{
    uint64_t ret;
    __asm__ volatile (
        "syscall"
        : "=a"(ret)
        : "a"(num), "D"(arg1)
        : "rcx", "r11", "memory"
    );
    return ret;
}

static inline uint64_t syscall2(uint64_t num, uint64_t arg1, uint64_t arg2)
{
    uint64_t ret;
    __asm__ volatile (
        "syscall"
        : "=a"(ret)
        : "a"(num), "D"(arg1), "S"(arg2)
        : "rcx", "r11", "memory"
    );
    return ret;
}

static inline uint64_t syscall3(uint64_t num, uint64_t arg1, uint64_t arg2, uint64_t arg3)
{
    uint64_t ret;
    __asm__ volatile (
        "syscall"
        : "=a"(ret)
        : "a"(num), "D"(arg1), "S"(arg2), "d"(arg3)
        : "rcx", "r11", "memory"
    );
    return ret;
}

static inline uint64_t syscall4(uint64_t num, uint64_t arg1, uint64_t arg2, uint64_t arg3, uint64_t arg4)
{
    uint64_t ret;
    register uint64_t r10 __asm__("r10") = arg4;
    __asm__ volatile (
        "syscall"
        : "=a"(ret)
        : "a"(num), "D"(arg1), "S"(arg2), "d"(arg3), "r"(r10)
        : "rcx", "r11", "memory"
    );
    return ret;
}

static void serial_write_string(const char *str)
{
    (void)syscall1(SYSCALL_SERIAL_PUTS, (uint64_t)str);
}

static int32_t thread_create(void (*entry)(void))
{
    return (int32_t)syscall1(SYSCALL_THREAD_CREATE, (uint64_t)entry);
}

static void process_yield(void)
{
    (void)syscall0(SYSCALL_PROCESS_YIELD);
}

static void draw_fill_rect(uint32_t x, uint32_t y, uint32_t w, uint32_t h, uint32_t color)
{
    uint64_t packed_wh = ((uint64_t)w << 32) | (uint64_t)h;
    (void)syscall4(SYSCALL_DRAW_FILL_RECT, x, y, packed_wh, color);
}

static void draw_present(void)
{
    (void)syscall0(SYSCALL_DRAW_PRESENT);
}

__attribute__((unused)) int32_t file_open(const char *path, uint64_t flags)
{
    return (int32_t)syscall2(SYSCALL_FILE_OPEN, (uint64_t)path, flags);
}

__attribute__((unused)) int64_t file_read(int32_t fd, void *buffer, uint64_t len)
{
    return (int64_t)syscall3(SYSCALL_FILE_READ, (uint64_t)fd, (uint64_t)buffer, len);
}

__attribute__((unused)) int64_t file_write(int32_t fd, const void *buffer, uint64_t len)
{
    return (int64_t)syscall3(SYSCALL_FILE_WRITE, (uint64_t)fd, (uint64_t)buffer, len);
}

__attribute__((unused)) int32_t file_close(int32_t fd)
{
    return (int32_t)syscall1(SYSCALL_FILE_CLOSE, (uint64_t)fd);
}

void* kmalloc(uint32_t size) {
    return (void*)syscall1(SYSCALL_USER_KMALLOC, size);
}

void kfree(void* ptr) {
    (void)syscall1(SYSCALL_USER_KFREE, (uint64_t)ptr);
}

__attribute__((noreturn))
static void process_exit(void)
{
    (void)syscall0(SYSCALL_PROCESS_EXIT);
    while (1) {
    }
}

#include "Application/PNG_Decoder/PNG_Decoder.h"

static void draw_png(const PNGImage *img, uint32_t x0, uint32_t y0) {
    for (uint32_t y=0; y<img->height; y++) {
        for (uint32_t x=0; x<img->width; x++) {
            uint32_t idx = (y*img->width + x) * 4;
            uint32_t color =
                (img->pixels[idx+0] << 24) |
                (img->pixels[idx+1] << 16) |
                (img->pixels[idx+2] << 8)  |
                (img->pixels[idx+3]);
            draw_fill_rect(x0+x, y0+y, 1, 1, color);
        }
    }
    draw_present();
}

void _start(void)
{
    serial_write_string("[U] userland start\n");

    PNGImage img;
    if (png_load("LOGO    PNG", &img) == 0) {
        draw_png(&img, 50, 50);
        png_free(&img);
    } else {
        serial_write_string("[U] failed to load PNG\n");
    }

    while (1) {
        //serial_write_string("[U] main process\n");
        process_yield();
    }
}