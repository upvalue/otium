#ifndef OT_KERNEL_DRV_GFX_VIRTIO_HPP
#define OT_KERNEL_DRV_GFX_VIRTIO_HPP

#include "ot/core/drivers/drv-virtio.hpp"
#include "ot/core/drivers/gfx.hpp"

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

class VirtioGfx : public Gfx {
public:
  VirtIODevice dev;
  VirtQueue controlq;
  PageAddr framebuffer;
  PageAddr cmd_page;  // Reusable command page
  PageAddr resp_page; // Reusable response page
  uint32_t width;
  uint32_t height;

  VirtioGfx() : dev(0), width(1024), height(600) {}
  VirtioGfx(uintptr_t addr) : dev(addr), width(1024), height(600) {}

  bool init() override {
    if (!dev.is_valid()) {
      oprintf("GPU: Device not valid\n");
      return false;
    }

    // Read device ID
    dev.device_id = dev.read_reg(VIRTIO_MMIO_DEVICE_ID);
    if (dev.device_id != VIRTIO_ID_GPU) {
      oprintf("GPU: Not a GPU device (id=%u)\n", dev.device_id);
      return false;
    }

    oprintf("Initializing VirtIO GPU...\n");

    // Check version
    uint32_t version = dev.read_reg(VIRTIO_MMIO_VERSION);
    oprintf("VirtIO version: %u\n", version);
    if (version != 1 && version != 2) {
      oprintf("GPU: Unsupported version\n");
      return false;
    }

    // Reset device
    dev.write_reg(VIRTIO_MMIO_STATUS, 0);

    // Set ACKNOWLEDGE status bit
    dev.write_reg(VIRTIO_MMIO_STATUS, VIRTIO_STATUS_ACKNOWLEDGE);

    // Set DRIVER status bit
    dev.write_reg(VIRTIO_MMIO_STATUS, VIRTIO_STATUS_ACKNOWLEDGE | VIRTIO_STATUS_DRIVER);

    // Negotiate features (we accept whatever is offered for simplicity)
    dev.write_reg(VIRTIO_MMIO_DRIVER_FEATURES, 0);

    // Set FEATURES_OK
    dev.write_reg(VIRTIO_MMIO_STATUS, VIRTIO_STATUS_ACKNOWLEDGE | VIRTIO_STATUS_DRIVER | VIRTIO_STATUS_FEATURES_OK);

    // Check FEATURES_OK
    if (!(dev.read_reg(VIRTIO_MMIO_STATUS) & VIRTIO_STATUS_FEATURES_OK)) {
      oprintf("GPU: Feature negotiation failed\n");
      return false;
    }

    // Setup controlq (queue 0)
    dev.write_reg(VIRTIO_MMIO_QUEUE_SEL, 0);
    uint32_t max_queue_size = dev.read_reg(VIRTIO_MMIO_QUEUE_NUM_MAX);
    oprintf("Queue 0 max size: %u\n", max_queue_size);
    if (max_queue_size == 0) {
      oprintf("GPU: Queue 0 not available\n");
      return false;
    }
    if (QUEUE_SIZE > max_queue_size) {
      oprintf("GPU: QUEUE_SIZE (%u) > max (%u)\n", QUEUE_SIZE, max_queue_size);
      return false;
    }

    // Allocate memory for the queue
    PageAddr queue_mem = page_allocate(current_proc->pid, 1);
    controlq.init(queue_mem, QUEUE_SIZE);

    oprintf("Queue physical addr: 0x%x\n", queue_mem.raw());

    // Configure queue (version-specific)
    dev.write_reg(VIRTIO_MMIO_QUEUE_NUM, QUEUE_SIZE);

    if (version == 1) {
      // Legacy interface: use guest page size and PFN
      dev.write_reg(VIRTIO_MMIO_GUEST_PAGE_SIZE, OT_PAGE_SIZE);
      dev.write_reg(VIRTIO_MMIO_QUEUE_ALIGN, OT_PAGE_SIZE);
      dev.write_reg(VIRTIO_MMIO_QUEUE_PFN, queue_mem.raw() / OT_PAGE_SIZE);
      oprintf("Legacy mode: PFN = 0x%x\n", queue_mem.raw() / OT_PAGE_SIZE);
    } else {
      // Modern interface: use separate desc/avail/used addresses
      dev.write_reg(VIRTIO_MMIO_QUEUE_DESC_LOW, (uint32_t)(uintptr_t)controlq.desc);
      dev.write_reg(VIRTIO_MMIO_QUEUE_DESC_HIGH, 0);
      dev.write_reg(VIRTIO_MMIO_QUEUE_DRIVER_LOW, (uint32_t)(uintptr_t)controlq.avail);
      dev.write_reg(VIRTIO_MMIO_QUEUE_DRIVER_HIGH, 0);
      dev.write_reg(VIRTIO_MMIO_QUEUE_DEVICE_LOW, (uint32_t)(uintptr_t)controlq.used);
      dev.write_reg(VIRTIO_MMIO_QUEUE_DEVICE_HIGH, 0);
      dev.write_reg(VIRTIO_MMIO_QUEUE_READY, 1);
    }

    // Set DRIVER_OK
    dev.write_reg(VIRTIO_MMIO_STATUS, VIRTIO_STATUS_ACKNOWLEDGE | VIRTIO_STATUS_DRIVER | VIRTIO_STATUS_FEATURES_OK |
                                          VIRTIO_STATUS_DRIVER_OK);

    oprintf("Status after DRIVER_OK: 0x%x\n", dev.read_reg(VIRTIO_MMIO_STATUS));

    oprintf("GPU: Initialization complete\n");
    return true;
  }

  uint32_t send_command(PageAddr cmd, uint32_t cmd_len, PageAddr resp, uint32_t resp_len) {
    // Zero out response buffer
    omemset(resp.as_ptr(), 0, resp_len);

    // Use descriptor 0 for command, descriptor 1 for response
    controlq.add_buf(0, cmd, cmd_len, false);
    controlq.add_buf(1, resp, resp_len, true);

    // Chain descriptors
    controlq.desc[0].flags |= VIRTQ_DESC_F_NEXT;
    controlq.desc[0].next = 1;

    uint16_t avail_idx_before = controlq.avail->idx;
    uint16_t used_idx_before = controlq.used->idx;

    // Submit to queue
    controlq.submit(0);

    oprintf("  avail idx: %u->%u, used idx: %u\n", avail_idx_before, controlq.avail->idx, used_idx_before);
    // Dump raw descriptor bytes
    uint8_t *desc0_bytes = (uint8_t *)&controlq.desc[0];
    oprintf("  desc[0] raw bytes: ");
    for (int i = 0; i < 16; i++) {
      oprintf("%02x ", desc0_bytes[i]);
    }
    oprintf("\n");
    oprintf("  desc[0]: addr=0x%llx, len=%u, flags=0x%x, next=%u\n", controlq.desc[0].addr, controlq.desc[0].len,
            controlq.desc[0].flags, controlq.desc[0].next);
    oprintf("  avail->ring[0]=%u\n", controlq.avail->ring[0]);

    // Notify device
    dev.write_reg(VIRTIO_MMIO_QUEUE_NOTIFY, 0);

    // Wait for response (simple polling)
    uint32_t timeout = 1000000;
    while (!controlq.has_used() && timeout-- > 0) {
      // Busy wait
    }

    if (timeout == 0) {
      oprintf("GPU: Command timeout! used idx still: %u\n", controlq.used->idx);
      return 0xFFFFFFFF;
    }

    oprintf("  Response received, used idx: %u\n", controlq.used->idx);
    controlq.get_used();

    // Return response type
    struct virtio_gpu_ctrl_hdr *resp_hdr = resp.as<struct virtio_gpu_ctrl_hdr>();
    oprintf("  Response type: 0x%x, flags: 0x%x\n", resp_hdr->type, resp_hdr->flags);
    return resp_hdr->type;
  }

  void create_framebuffer() {
    oprintf("Creating framebuffer (%ux%u)...\n", width, height);

    // Allocate framebuffer memory (need multiple pages)
    uint32_t fb_size = width * height * 4; // 4 bytes per pixel (BGRA)
    uint32_t fb_pages = (fb_size + OT_PAGE_SIZE - 1) / OT_PAGE_SIZE;
    framebuffer = page_allocate(current_proc->pid, fb_pages);

    oprintf("Allocated %u pages for framebuffer at 0x%x\n", fb_pages, framebuffer.raw());

    // Allocate command/response pages (reused for all commands)
    cmd_page = page_allocate(current_proc->pid, 1);
    resp_page = page_allocate(current_proc->pid, 1);

    oprintf("CMD page: 0x%x, RESP page: 0x%x\n", cmd_page.raw(), resp_page.raw());

    struct virtio_gpu_resource_create_2d *cmd = cmd_page.as<struct virtio_gpu_resource_create_2d>();
    omemset(cmd, 0, sizeof(*cmd));
    cmd->hdr.type = VIRTIO_GPU_CMD_RESOURCE_CREATE_2D;
    cmd->hdr.flags = 0;
    cmd->hdr.fence_id = 0;
    cmd->hdr.ctx_id = 0;
    cmd->hdr.padding = 0;
    cmd->resource_id = 1;
    cmd->format = VIRTIO_GPU_FORMAT_B8G8R8A8_UNORM;
    cmd->width = width;
    cmd->height = height;

    oprintf("Sending CREATE_2D: res_id=%u, fmt=%u, %ux%u\n", cmd->resource_id, cmd->format, cmd->width, cmd->height);

    uint32_t resp_type = send_command(cmd_page, sizeof(*cmd), resp_page, sizeof(struct virtio_gpu_ctrl_hdr));
    oprintf("Resource create response: 0x%x\n", resp_type);
    if (resp_type != VIRTIO_GPU_RESP_OK_NODATA) {
      oprintf("ERROR: Resource creation failed!\n");
      return;
    }

    // Attach backing store
    struct virtio_gpu_resource_attach_backing *attach_cmd = cmd_page.as<struct virtio_gpu_resource_attach_backing>();
    attach_cmd->hdr.type = VIRTIO_GPU_CMD_RESOURCE_ATTACH_BACKING;
    attach_cmd->hdr.flags = 0;
    attach_cmd->hdr.fence_id = 0;
    attach_cmd->hdr.ctx_id = 0;
    attach_cmd->hdr.padding = 0;
    attach_cmd->resource_id = 1;
    attach_cmd->nr_entries = 1;

    struct virtio_gpu_mem_entry *entry = (struct virtio_gpu_mem_entry *)(attach_cmd + 1);
    entry->addr = framebuffer.raw();
    entry->length = fb_size;
    entry->padding = 0;

    resp_type = send_command(cmd_page, sizeof(*attach_cmd) + sizeof(struct virtio_gpu_mem_entry), resp_page,
                             sizeof(struct virtio_gpu_ctrl_hdr));
    oprintf("Attach backing response: 0x%x\n", resp_type);
    if (resp_type != VIRTIO_GPU_RESP_OK_NODATA) {
      oprintf("ERROR: Attach backing failed!\n");
      return;
    }

    // Set scanout
    struct virtio_gpu_set_scanout *scanout = cmd_page.as<struct virtio_gpu_set_scanout>();
    scanout->hdr.type = VIRTIO_GPU_CMD_SET_SCANOUT;
    scanout->hdr.flags = 0;
    scanout->hdr.fence_id = 0;
    scanout->hdr.ctx_id = 0;
    scanout->hdr.padding = 0;
    scanout->r.x = 0;
    scanout->r.y = 0;
    scanout->r.width = width;
    scanout->r.height = height;
    scanout->scanout_id = 0;
    scanout->resource_id = 1;

    resp_type = send_command(cmd_page, sizeof(*scanout), resp_page, sizeof(struct virtio_gpu_ctrl_hdr));
    oprintf("Set scanout response: 0x%x\n", resp_type);
    if (resp_type != VIRTIO_GPU_RESP_OK_NODATA) {
      oprintf("ERROR: Set scanout failed!\n");
      return;
    }

    oprintf("Framebuffer setup complete, ready for drawing\n");
  }

  void put(uint32_t x, uint32_t y, Color color) override {
    if (x >= width || y >= height)
      return;
    uint32_t *fb = framebuffer.as<uint32_t>();
    fb[y * width + x] = color;
  }

  void fill_rect(uint32_t x, uint32_t y, uint32_t w, uint32_t h, Color color) override {
    for (uint32_t dy = 0; dy < h; dy++) {
      for (uint32_t dx = 0; dx < w; dx++) {
        put(x + dx, y + dy, color);
      }
    }
  }

  void flush() override {
    // Reuse existing command/response pages
    // Transfer to host
    struct virtio_gpu_transfer_to_host_2d *transfer = cmd_page.as<struct virtio_gpu_transfer_to_host_2d>();
    transfer->hdr.type = VIRTIO_GPU_CMD_TRANSFER_TO_HOST_2D;
    transfer->hdr.flags = 0;
    transfer->hdr.fence_id = 0;
    transfer->hdr.ctx_id = 0;
    transfer->hdr.padding = 0;
    transfer->r.x = 0;
    transfer->r.y = 0;
    transfer->r.width = width;
    transfer->r.height = height;
    transfer->offset = 0;
    transfer->resource_id = 1;
    transfer->padding = 0;

    uint32_t resp_type = send_command(cmd_page, sizeof(*transfer), resp_page, sizeof(struct virtio_gpu_ctrl_hdr));
    oprintf("Transfer response: 0x%x\n", resp_type);
    if (resp_type != VIRTIO_GPU_RESP_OK_NODATA) {
      oprintf("ERROR: Transfer failed!\n");
      return;
    }

    // Flush resource
    struct virtio_gpu_resource_flush *flush_cmd = cmd_page.as<struct virtio_gpu_resource_flush>();
    flush_cmd->hdr.type = VIRTIO_GPU_CMD_RESOURCE_FLUSH;
    flush_cmd->hdr.flags = 0;
    flush_cmd->hdr.fence_id = 0;
    flush_cmd->hdr.ctx_id = 0;
    flush_cmd->hdr.padding = 0;
    flush_cmd->r.x = 0;
    flush_cmd->r.y = 0;
    flush_cmd->r.width = width;
    flush_cmd->r.height = height;
    flush_cmd->resource_id = 1;
    flush_cmd->padding = 0;

    resp_type = send_command(cmd_page, sizeof(*flush_cmd), resp_page, sizeof(struct virtio_gpu_ctrl_hdr));
    oprintf("Flush response: 0x%x\n", resp_type);
    if (resp_type != VIRTIO_GPU_RESP_OK_NODATA) {
      oprintf("ERROR: Flush failed!\n");
      return;
    }
  }

  uint32_t get_width() const override { return width; }

  uint32_t get_height() const override { return height; }
};

void graphics_demo_main_proc();

#endif
