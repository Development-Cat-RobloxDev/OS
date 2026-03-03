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

// --- 文字列 ---

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

// --- アサート ---

void __assert_fail(const char* expr, const char* file, unsigned int line, const char* func) {
    (void)expr; (void)file; (void)line; (void)func;
    // カーネルパニック相当: 割り込み禁止後ハング
    __asm__ volatile("cli");
    for (;;) __asm__ volatile("hlt");
}

// --- 数学 (カーネル用簡易実装) ---

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

// sqrt: ニュートン法
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

// cos: テイラー展開 ([-π, π] で十分な精度)
#define M_PI_APPROX 3.14159265358979323846

double cos(double x) {
    // x を [-π, π] に正規化
    x = fmod(x, 2.0 * M_PI_APPROX);
    if (x > M_PI_APPROX)  x -= 2.0 * M_PI_APPROX;
    if (x < -M_PI_APPROX) x += 2.0 * M_PI_APPROX;

    double x2 = x * x;
    // cos(x) ≈ 1 - x²/2! + x⁴/4! - x⁶/6! + x⁸/8! - x¹⁰/10!
    return 1.0
         - x2 / 2.0
         + x2*x2 / 24.0
         - x2*x2*x2 / 720.0
         + x2*x2*x2*x2 / 40320.0
         - x2*x2*x2*x2*x2 / 3628800.0;
}

// acos: acos(x) = atan2(sqrt(1-x²), x)  ←  atan の近似で実装
static double _atan(double x) {
    // ミニマックス近似 (|x| <= 1 前提)
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
    // acos(x) = π/2 - asin(x),  asin(x) = atan(x / sqrt(1-x²))
    double s = sqrt(1.0 - x * x);
    double angle;
    if (fabs(x) <= 0.7071067811865476 /* 1/√2 */) {
        angle = M_PI_APPROX / 2.0 - _atan(x / s);
    } else {
        // |x| が 1 に近いときは s/x で計算して精度向上
        angle = _atan(s / x);
        if (x < 0.0) angle += M_PI_APPROX;
    }
    return angle;
}

// pow: 整数指数は既存実装, 小数指数は exp(y*ln(x)) で近似
static double _ln(double x) {
    // ln(x) = 2 * atanh((x-1)/(x+1))  atanh は _atan の引数変換で対応
    if (x <= 0.0) return 0.0;
    // 正規化: x = m * 2^e で m ∈ [0.5, 1)
    int e = 0;
    double m = x;
    while (m >= 1.0) { m /= 2.0; e++; }
    while (m < 0.5)  { m *= 2.0; e--; }
    // ln(m) via atanh series (m ∈ [0.5,1))
    double t = (m - 1.0) / (m + 1.0);
    double t2 = t * t, s = t;
    double term = t;
    for (int k = 1; k <= 20; k++) {
        term *= t2;
        s += term / (2*k + 1);
    }
    return 2.0 * s + (double)e * 0.6931471805599453; // + e*ln(2)
}

static double _exp(double x) {
    // exp(x) = e^x, テイラー展開
    double sum = 1.0, term = 1.0;
    for (int k = 1; k <= 30; k++) {
        term *= x / k;
        sum += term;
        if (fabs(term) < 1e-17) break;
    }
    return sum;
}

// pow を既存の整数実装から置き換え（小数指数対応版）
// ※ LibC.c の既存 pow() と重複するため、既存の pow() は削除してこちらに統一
double pow(double x, double y) {
    if (y == 0.0) return 1.0;
    if (x == 0.0) return 0.0;
    // 整数指数の場合は高速パス
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