/* virtio-gpu in 2D mode: one resource in guest memory, set as scanout 0 and
 * copied to the host on each flush. */
#include "display.h"
#include "virtio.h"

#include <stdlib.h>
#include <string.h>

enum {
  CMD_GET_DISPLAY_INFO = 0x0100,
  CMD_RESOURCE_CREATE_2D = 0x0101,
  CMD_SET_SCANOUT = 0x0103,
  CMD_RESOURCE_FLUSH = 0x0104,
  CMD_TRANSFER_TO_HOST_2D = 0x0105,
  CMD_RESOURCE_ATTACH_BACKING = 0x0106,
  RESP_OK_NODATA = 0x1100,
  RESP_OK_DISPLAY_INFO = 0x1101,
};

/* Little-endian 0x00RRGGBB. */
#define FORMAT_B8G8R8X8_UNORM 2
#define RESOURCE_ID 1

typedef struct {
  uint32_t type;
  uint32_t flags;
  uint64_t fence_id;
  uint32_t context_id;
  uint8_t ring_index;
  uint8_t padding[3];
} gpu_header;

typedef struct {
  uint32_t x, y, width, height;
} gpu_rect;

typedef struct {
  gpu_header header;
  struct {
    gpu_rect rect;
    uint32_t enabled;
    uint32_t flags;
  } displays[16];
} gpu_display_info;

typedef struct {
  gpu_header header;
  uint32_t resource_id;
  uint32_t format;
  uint32_t width;
  uint32_t height;
} gpu_resource_create_2d;

typedef struct {
  gpu_header header;
  uint32_t resource_id;
  uint32_t entry_count;
  uint64_t address;
  uint32_t length;
  uint32_t padding;
} gpu_attach_backing;

typedef struct {
  gpu_header header;
  gpu_rect rect;
  uint32_t scanout_id;
  uint32_t resource_id;
} gpu_set_scanout;

typedef struct {
  gpu_header header;
  gpu_rect rect;
  uint64_t offset;
  uint32_t resource_id;
  uint32_t padding;
} gpu_transfer_to_host_2d;

typedef struct {
  gpu_header header;
  gpu_rect rect;
  uint32_t resource_id;
  uint32_t padding;
} gpu_resource_flush;

static virtio_device gpu;
static display screen;
static bool opened, available;

static bool send(void* command, size_t length, uint32_t type) {
  gpu_header response;
  ((gpu_header*)command)->type = type;
  virtio_request(&gpu, command, length, &response, sizeof response);
  return response.type == RESP_OK_NODATA;
}

static bool open_display(void) {
  if (!virtio_open(&gpu, VIRTIO_ID_GPU)) return false;

  gpu_header query = {.type = CMD_GET_DISPLAY_INFO};
  static gpu_display_info info;
  virtio_request(&gpu, &query, sizeof query, &info, sizeof info);
  if (info.header.type != RESP_OK_DISPLAY_INFO || !info.displays[0].enabled) return false;
  gpu_rect rect = info.displays[0].rect;

  size_t bytes = (size_t)rect.width * rect.height * sizeof(uint32_t);
  uint32_t* pixels = aligned_alloc(4096, (bytes + 4095) / 4096 * 4096);
  if (pixels == NULL) return false;
  memset(pixels, 0, bytes);

  gpu_resource_create_2d create = {.resource_id = RESOURCE_ID,
                                   .format = FORMAT_B8G8R8X8_UNORM,
                                   .width = rect.width,
                                   .height = rect.height};
  gpu_attach_backing attach = {
      .resource_id = RESOURCE_ID, .entry_count = 1, .address = (uintptr_t)pixels, .length = bytes};
  gpu_set_scanout scanout = {.rect = rect, .scanout_id = 0, .resource_id = RESOURCE_ID};
  if (!send(&create, sizeof create, CMD_RESOURCE_CREATE_2D) ||
      !send(&attach, sizeof attach, CMD_RESOURCE_ATTACH_BACKING) ||
      !send(&scanout, sizeof scanout, CMD_SET_SCANOUT)) {
    free(pixels);
    return false;
  }
  screen = (display){.pixels = pixels, .width = (int)rect.width, .height = (int)rect.height};
  display_flush(&screen);
  return true;
}

display* display_get(void) {
  if (!opened) {
    opened = true;
    available = open_display();
  }
  return available ? &screen : NULL;
}

static uint32_t blend(uint32_t under, uint32_t over, uint32_t alpha) {
  uint32_t result = 0;
  for (int shift = 0; shift < 24; shift += 8) {
    uint32_t top = over >> shift & 0xff;
    uint32_t bottom = under >> shift & 0xff;
    result |= (top * alpha + bottom * (255 - alpha) + 127) / 255 << shift;
  }
  return result;
}

void display_fill_rect(display* d, int x, int y, int width, int height, uint32_t color) {
  int left = x < 0 ? 0 : x;
  int top = y < 0 ? 0 : y;
  int right = width > d->width - x ? d->width : x + width;
  int bottom = height > d->height - y ? d->height : y + height;
  uint32_t rgb = color >> 8;
  uint32_t alpha = color & 0xff;
  if (alpha == 0) return;
  for (int row = top; row < bottom; row++) {
    uint32_t* line = d->pixels + (size_t)row * d->width;
    if (alpha == 255) {
      for (int column = left; column < right; column++) line[column] = rgb;
    } else {
      for (int column = left; column < right; column++)
        line[column] = blend(line[column], rgb, alpha);
    }
  }
}

void display_flush(display* d) {
  gpu_rect rect = {0, 0, (uint32_t)d->width, (uint32_t)d->height};
  gpu_transfer_to_host_2d transfer = {.rect = rect, .resource_id = RESOURCE_ID};
  gpu_resource_flush flush = {.rect = rect, .resource_id = RESOURCE_ID};
  send(&transfer, sizeof transfer, CMD_TRANSFER_TO_HOST_2D);
  send(&flush, sizeof flush, CMD_RESOURCE_FLUSH);
}
