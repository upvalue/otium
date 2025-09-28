// support.c -- some C support
extern void host_print(const char *ptr, int len);

#include <stddef.h>

typedef unsigned char uint8_t;

void *memcpy(void *dst, const void *src, size_t n)
{
    uint8_t *d = (uint8_t *)dst;
    const uint8_t *s = (const uint8_t *)src;
    while (n--)
        *d++ = *s++;
    return dst;
}

#include "../c/tlsf.c"

static uint8_t memory[1024 * 1024];
static tlsf_t memory_tlsf;

void c_init()
{
    memory_tlsf = tlsf_create_with_pool(memory_tlsf, 1024 * 1024);
}

// Helper function to calculate string length
int c_strlen(const char *str)
{
    int len = 0;
    while (str[len] != '\0')
    {
        len++;
    }
    return len;
}

void hello()
{
    const char *message = "Hello World from WASM C!";
    host_print(message, c_strlen(message));
    host_print("\n", 1);
}