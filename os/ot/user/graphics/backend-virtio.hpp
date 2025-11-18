#ifndef OT_USER_GRAPHICS_BACKEND_VIRTIO_HPP
#define OT_USER_GRAPHICS_BACKEND_VIRTIO_HPP

#include "ot/user/graphics/backend.hpp"
#include "ot/user/virtio/virtio.hpp"

// VirtIO GPU commands
#define VIRTIO_GPU_CMD_GET_DISPLAY_INFO 0x0100
#define VIRTIO_GPU_CMD_RESOURCE_CREATE_2D 0x0101
#define VIRTIO_GPU_CMD_RESOURCE_UNREF 0x0102
#define VIRTIO_GPU_CMD_SET_SCANOUT 0x0103
#define VIRTIO_GPU_CMD_RESOURCE_FLUSH 0x0104
#define VIRTIO_GPU_CMD_TRANSFER_TO_HOST_2D 0x0105
#define VIRTIO_GPU_CMD_RESOURCE_ATTACH_BACKING 0x0106
#define VIRTIO_GPU_CMD_RESOURCE_DETACH_BACKING 0x0107

// VirtIO GPU response codes
#define VIRTIO_GPU_RESP_OK_NODATA 0x1100
#define VIRTIO_GPU_RESP_OK_DISPLAY_INFO 0x1101
#define VIRTIO_GPU_RESP_ERR_UNSPEC 0x1200
#define VIRTIO_GPU_RESP_ERR_OUT_OF_MEMORY 0x1201
#define VIRTIO_GPU_RESP_ERR_INVALID_SCANOUT_ID 0x1202

// VirtIO GPU formats
#define VIRTIO_GPU_FORMAT_B8G8R8A8_UNORM 1

// VirtIO GPU command header
struct virtio_gpu_ctrl_hdr {
  uint32_t type;
  uint32_t flags;
  uint64_t fence_id;
  uint32_t ctx_id;
  uint32_t padding;
} __attribute__((packed));

// VirtIO GPU rectangle
struct virtio_gpu_rect {
  uint32_t x;
  uint32_t y;
  uint32_t width;
  uint32_t height;
} __attribute__((packed));

// VirtIO GPU resource create 2D
struct virtio_gpu_resource_create_2d {
  struct virtio_gpu_ctrl_hdr hdr;
  uint32_t resource_id;
  uint32_t format;
  uint32_t width;
  uint32_t height;
} __attribute__((packed));

// VirtIO GPU set scanout
struct virtio_gpu_set_scanout {
  struct virtio_gpu_ctrl_hdr hdr;
  struct virtio_gpu_rect r;
  uint32_t scanout_id;
  uint32_t resource_id;
} __attribute__((packed));

// VirtIO GPU transfer to host 2D
struct virtio_gpu_transfer_to_host_2d {
  struct virtio_gpu_ctrl_hdr hdr;
  struct virtio_gpu_rect r;
  uint64_t offset;
  uint32_t resource_id;
  uint32_t padding;
} __attribute__((packed));

// VirtIO GPU resource flush
struct virtio_gpu_resource_flush {
  struct virtio_gpu_ctrl_hdr hdr;
  struct virtio_gpu_rect r;
  uint32_t resource_id;
  uint32_t padding;
} __attribute__((packed));

// VirtIO GPU resource attach backing
struct virtio_gpu_resource_attach_backing {
  struct virtio_gpu_ctrl_hdr hdr;
  uint32_t resource_id;
  uint32_t nr_entries;
} __attribute__((packed));

// VirtIO GPU memory entry
struct virtio_gpu_mem_entry {
  uint64_t addr;
  uint32_t length;
  uint32_t padding;
} __attribute__((packed));

class VirtioGraphicsBackend : public GraphicsBackend {
public:
  VirtIODevice dev;
  VirtQueue controlq;
  PageAddr framebuffer;
  PageAddr cmd_page;  // Reusable command page
  PageAddr resp_page; // Reusable response page
  uint32_t width;
  uint32_t height;

  VirtioGraphicsBackend() : dev(0), width(1024), height(700) {}
  VirtioGraphicsBackend(uintptr_t addr) : dev(addr), width(1024), height(700) {}

  bool init() override;
  uint32_t *get_framebuffer() override { return framebuffer.as<uint32_t>(); }
  void flush() override;
  uint32_t get_width() const override { return width; }
  uint32_t get_height() const override { return height; }

private:
  uint32_t send_command(PageAddr cmd, uint32_t cmd_len, PageAddr resp, uint32_t resp_len);
  void create_framebuffer();
};

#endif
