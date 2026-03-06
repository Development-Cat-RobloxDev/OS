#include <stddef.h>
#include <stdint.h>
#include "../Memory/Memory_Main.h"

void* malloc(size_t size) {
    if (size > UINT32_MAX) return NULL;
    return kmalloc((uint32_t)size);
}

void free(void* ptr) {
    kfree(ptr);
}

void* memset(void *ptr, int value, size_t num) {
    unsigned char *p = (unsigned char*)ptr;
    for (size_t i = 0; i < num; i++)
        p[i] = (unsigned char)value;
    return ptr;
}

void* memcpy(void* dst, const void* src, size_t n) {
    uint8_t* d = dst;
    const uint8_t* s = src;
    for (size_t i = 0; i < n; i++) d[i] = s[i];
    return dst;
}

int memcmp(const void *s1, const void *s2, size_t n) {
    const unsigned char *p1 = (const unsigned char *)s1;
    const unsigned char *p2 = (const unsigned char *)s2;

    for (size_t i = 0; i < n; i++) {
        if (p1[i] != p2[i]) {
            return (int)p1[i] - (int)p2[i];
        }
    }

    return 0;
}

size_t strlen(const char* s) {
    size_t n = 0;
    while (s[n]) n++;
    return n;
}

char* strcpy(char* dst, const char* src) {
    char* d = dst;
    while ((*d++ = *src++));
    return dst;
}

char* strncpy(char* dst, const char* src, size_t n) {
    size_t i = 0;
    for (; i < n && src[i]; i++) dst[i] = src[i];
    for (; i < n; i++) dst[i] = '\0';
    return dst;
}

int strcmp(const char* a, const char* b) {
    while (*a && (*a == *b)) {
        a++;
        b++;
    }
    return (unsigned char)*a - (unsigned char)*b;
}

int strncmp(const char* a, const char* b, size_t n) {
    for (size_t i = 0; i < n; i++) {
        if (a[i] != b[i]) {
            return (unsigned char)a[i] - (unsigned char)b[i];
        }
        if (a[i] == '\0') return 0;
    }
    return 0;
}

long strtol(const char* nptr, char** endptr, int base) {
    while (*nptr == ' ' || *nptr == '\t' || *nptr == '\n' || *nptr == '\r') {
        nptr++;
    }

    long result = 0;
    int sign = 1;
    int auto_base = base;

    if (*nptr == '+') {
        nptr++;
    } else if (*nptr == '-') {
        sign = -1;
        nptr++;
    }

    if (auto_base == 0) {
        if (*nptr == '0') {
            if (*(nptr + 1) == 'x' || *(nptr + 1) == 'X') {
                auto_base = 16;
                nptr += 2;
            } else {
                auto_base = 8;
                nptr++;
            }
        } else {
            auto_base = 10;
        }
    } else if (auto_base == 16) {
        if (*nptr == '0' && (*(nptr + 1) == 'x' || *(nptr + 1) == 'X')) {
            nptr += 2;
        }
    }

    if (auto_base < 2 || auto_base > 36) {
        if (endptr) *endptr = (char*)nptr;
        return 0;
    }

    const char *start = nptr;
    while (*nptr) {
        int digit = -1;

        if (*nptr >= '0' && *nptr <= '9') {
            digit = *nptr - '0';
        } else if (*nptr >= 'a' && *nptr <= 'z') {
            digit = *nptr - 'a' + 10;
        } else if (*nptr >= 'A' && *nptr <= 'Z') {
            digit = *nptr - 'A' + 10;
        } else {
            break;
        }

        if (digit >= auto_base) {
            break;
        }

        result = result * auto_base + digit;
        nptr++;
    }

    if (nptr == start) {
        if (endptr) {
            if (sign == -1) {
                *endptr = (char*)(nptr - 1);
            } else {
                *endptr = (char*)start;
            }
        }
        return 0;
    }

    if (endptr) {
        *endptr = (char*)nptr;
    }

    return result * sign;
}

void __assert_fail(const char* expr, const char* file, unsigned int line, const char* func) {
    (void)expr; (void)file; (void)line; (void)func;
    __asm__ volatile("cli");
    for (;;) __asm__ volatile("hlt");
}

double fabs(double x) {
    return x < 0.0 ? -x : x;
}

double floor(double x) {
    long long i = (long long)x;
    return (double)(i - (x < (double)i ? 1 : 0));
}

double ceil(double x) {
    long long i = (long long)x;
    return (double)(i + (x > (double)i ? 1 : 0));
}

double fmod(double x, double y) {
    if (y == 0.0) return 0.0;
    long long n = (long long)(x / y);
    return x - (double)n * y;
}

double sqrt(double x) {
    if (x < 0.0) return 0.0;
    if (x == 0.0) return 0.0;
    double r = x > 1.0 ? x / 2.0 : 1.0;
    for (int i = 0; i < 60; i++) {
        double rn = (r + x / r) * 0.5;
        if (fabs(rn - r) < 1e-15 * r) { r = rn; break; }
        r = rn;
    }
    return r;
}

#define M_PI_APPROX 3.14159265358979323846

double cos(double x) {
    x = fmod(x, 2.0 * M_PI_APPROX);
    if (x > M_PI_APPROX)  x -= 2.0 * M_PI_APPROX;
    if (x < -M_PI_APPROX) x += 2.0 * M_PI_APPROX;

    double x2 = x * x;
    return 1.0
         - x2 / 2.0
         + x2*x2 / 24.0
         - x2*x2*x2 / 720.0
         + x2*x2*x2*x2 / 40320.0
         - x2*x2*x2*x2*x2 / 3628800.0;
}

static double _atan(double x) {
    double x2 = x * x;
    return x * (1.0
        - x2 * (1.0/3.0
        - x2 * (1.0/5.0
        - x2 * (1.0/7.0
        - x2 * (1.0/9.0
        - x2 * (1.0/11.0))))));
}

double acos(double x) {
    if (x >  1.0) x =  1.0;
    if (x < -1.0) x = -1.0;
    double s = sqrt(1.0 - x * x);
    double angle;
    if (fabs(x) <= 0.7071067811865476) {
        angle = M_PI_APPROX / 2.0 - _atan(x / s);
    } else {
        angle = _atan(s / x);
        if (x < 0.0) angle += M_PI_APPROX;
    }
    return angle;
}

static double _ln(double x) {
    if (x <= 0.0) return 0.0;
    int e = 0;
    double m = x;
    while (m >= 1.0) { m /= 2.0; e++; }
    while (m < 0.5)  { m *= 2.0; e--; }
    double t = (m - 1.0) / (m + 1.0);
    double t2 = t * t, s = t;
    double term = t;
    for (int k = 1; k <= 20; k++) {
        term *= t2;
        s += term / (2*k + 1);
    }
    return 2.0 * s + (double)e * 0.6931471805599453;
}

static double _exp(double x) {
    double sum = 1.0, term = 1.0;
    for (int k = 1; k <= 30; k++) {
        term *= x / k;
        sum += term;
        if (fabs(term) < 1e-17) break;
    }
    return sum;
}

double pow(double x, double y) {
    if (y == 0.0) return 1.0;
    if (x == 0.0) return 0.0;
    long long yi = (long long)y;
    if ((double)yi == y) {
        double r = 1.0;
        int neg = 0;
        if (yi < 0) { neg = 1; yi = -yi; }
        double base = x;
        while (yi > 0) {
            if (yi & 1) r *= base;
            base *= base;
            yi >>= 1;
        }
        return neg ? 1.0 / r : r;
    }
    return _exp(y * _ln(x));
}