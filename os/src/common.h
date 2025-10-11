#ifndef COMMON_H
#define COMMON_H

typedef unsigned char uint8_t;
typedef unsigned int uint32_t;
typedef uint32_t size_t;

void *memset(void *s, int c, size_t n);
void *memcpy(void *dest, const void *src, size_t n);
void *malloc(size_t size);
size_t strlen(const char *s);

void printf(const char *fmt, ...);

void putchar(char c);

#endif