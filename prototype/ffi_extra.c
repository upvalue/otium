#include <stdbool.h>
#include <stdint.h>

double abi_mix(int a, double x, int b, double y, int c, double z, int d, double u,
               int e, double v, int f, double w, int g, double q, int h, double r,
               void *pointer, bool flag, float tail) {
    return a+b+c+d+e+f+g+h+x+y+z+u+v+w+q+r+tail+(pointer ? 1000 : 0)+(flag ? 100 : 0);
}

const char *offset_text(const char *p) { return p + 1; }
void *offset_pointer(void *p) { return (unsigned char *)p + 1; }
int64_t lowest_integer(void) { return INT64_MIN; }
