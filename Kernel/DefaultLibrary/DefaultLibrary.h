#pragma once

#include <stdint.h>
#include <stddef.h>

void* malloc(size_t size);
void  free(void* ptr);
void* memset(void* ptr, int value, size_t num);
void* memcpy(void* dst, const void* src, size_t n);
int   memcmp(const void* s1, const void* s2, size_t n);
size_t strlen(const char* s);
char*  strcpy(char* dst, const char* src);
char*  strncpy(char* dst, const char* src, size_t n);
int    strcmp(const char* a, const char* b);
int    strncmp(const char* a, const char* b, size_t n);
long   strtol(const char* nptr, char** endptr, int base);
int abs(int x);
double ldexp(double x, int exp);
double fabs(double x);
double floor(double x);
double ceil(double x);
double fmod(double x, double y);
double sqrt(double x);
double pow(double x, double y);
double cos(double x);
double acos(double x);
void __assert_fail(const char* expr, const char* file,
                   unsigned int line, const char* func)
    __attribute__((noreturn));