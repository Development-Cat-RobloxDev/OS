#include <stdint.h>

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

static __attribute__((unused)) int32_t file_open(const char *path, uint64_t flags)
{
    return (int32_t)syscall2(SYSCALL_FILE_OPEN, (uint64_t)path, flags);
}

static __attribute__((unused)) int64_t file_read(int32_t fd, void *buffer, uint64_t len)
{
    return (int64_t)syscall3(SYSCALL_FILE_READ, (uint64_t)fd, (uint64_t)buffer, len);
}

static __attribute__((unused)) int64_t file_write(int32_t fd, const void *buffer, uint64_t len)
{
    return (int64_t)syscall3(SYSCALL_FILE_WRITE, (uint64_t)fd, (uint64_t)buffer, len);
}

static __attribute__((unused)) int32_t file_close(int32_t fd)
{
    return (int32_t)syscall1(SYSCALL_FILE_CLOSE, (uint64_t)fd);
}

__attribute__((noreturn))
static void process_exit(void)
{
    (void)syscall0(SYSCALL_PROCESS_EXIT);
    while (1) {
    }
}

__attribute__((noreturn))
void _start(void)
{
    serial_write_string("[U] userland start\n");
    draw_fill_rect(50, 50, 100, 250, 0xFFFFFFFF);
    draw_present();
    while (1) {
        serial_write_string("[U] main process\n");
        process_yield();
    }
}
