#ifndef OTOS_DISPLAY_H
#define OTOS_DISPLAY_H

#include <stdint.h>

/* A framebuffer shown through virtio-gpu. Pixels are 0x00RRGGBB, row by row. */
typedef struct display {
  uint32_t* pixels;
  int width;
  int height;
} display;

/* The display, opened on first use; NULL when there is no virtio-gpu device. */
display* display_get(void);
/* Blends color, 0xRRGGBBAA, over the rectangle clipped to the screen. */
void display_fill_rect(display* d, int x, int y, int width, int height, uint32_t color);
/* Copies the framebuffer to the host and shows it. */
void display_flush(display* d);

#endif
