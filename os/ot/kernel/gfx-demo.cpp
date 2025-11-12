#include <stdint.h>

#include "ot/kernel/kernel.hpp"

// VirtIO MMIO register offsets
#define VIRTIO_MMIO_MAGIC_VALUE 0x000
#define VIRTIO_MMIO_VERSION 0x004
#define VIRTIO_MMIO_DEVICE_ID 0x008
#define VIRTIO_MMIO_VENDOR_ID 0x00c
#define VIRTIO_MMIO_DEVICE_FEATURES 0x010
#define VIRTIO_MMIO_DEVICE_FEATURES_SEL 0x014
#define VIRTIO_MMIO_DRIVER_FEATURES 0x020
#define VIRTIO_MMIO_DRIVER_FEATURES_SEL 0x024
#define VIRTIO_MMIO_GUEST_PAGE_SIZE 0x028 // Legacy only
#define VIRTIO_MMIO_QUEUE_SEL 0x030
#define VIRTIO_MMIO_QUEUE_NUM_MAX 0x034
#define VIRTIO_MMIO_QUEUE_NUM 0x038
#define VIRTIO_MMIO_QUEUE_ALIGN 0x03c // Legacy only
#define VIRTIO_MMIO_QUEUE_PFN 0x040   // Legacy only
#define VIRTIO_MMIO_QUEUE_READY 0x044 // Modern only
#define VIRTIO_MMIO_QUEUE_NOTIFY 0x050
#define VIRTIO_MMIO_STATUS 0x070
#define VIRTIO_MMIO_QUEUE_DESC_LOW 0x080    // Modern only
#define VIRTIO_MMIO_QUEUE_DESC_HIGH 0x084   // Modern only
#define VIRTIO_MMIO_QUEUE_DRIVER_LOW 0x090  // Modern only
#define VIRTIO_MMIO_QUEUE_DRIVER_HIGH 0x094 // Modern only
#define VIRTIO_MMIO_QUEUE_DEVICE_LOW 0x0a0  // Modern only
#define VIRTIO_MMIO_QUEUE_DEVICE_HIGH 0x0a4 // Modern only

// VirtIO status bits
#define VIRTIO_STATUS_ACKNOWLEDGE 1
#define VIRTIO_STATUS_DRIVER 2
#define VIRTIO_STATUS_DRIVER_OK 4
#define VIRTIO_STATUS_FEATURES_OK 8
#define VIRTIO_STATUS_FAILED 128

// VirtIO device IDs
#define VIRTIO_ID_GPU 16

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

// QEMU RISC-V virt machine MMIO range
#define VIRTIO_MMIO_BASE 0x10001000
#define VIRTIO_MMIO_SIZE 0x1000
#define VIRTIO_MMIO_COUNT 8

// Virtqueue descriptor flags
#define VIRTQ_DESC_F_NEXT 1
#define VIRTQ_DESC_F_WRITE 2

#define QUEUE_SIZE 8

// Virtqueue descriptor
struct virtq_desc {
  uint64_t addr;
  uint32_t len;
  uint16_t flags;
  uint16_t next;
} __attribute__((packed));

// Virtqueue available ring
struct virtq_avail {
  uint16_t flags;
  uint16_t idx;
  uint16_t ring[QUEUE_SIZE];
  uint16_t used_event;
} __attribute__((packed));

// Virtqueue used element
struct virtq_used_elem {
  uint32_t id;
  uint32_t len;
} __attribute__((packed));

// Virtqueue used ring
struct virtq_used {
  uint16_t flags;
  uint16_t idx;
  struct virtq_used_elem ring[QUEUE_SIZE];
  uint16_t avail_event;
} __attribute__((packed));

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

class VirtIODevice {
public:
  volatile uint32_t *base;
  uint32_t device_id;
  uint32_t vendor_id;

  VirtIODevice(uintptr_t addr) : base((volatile uint32_t *)addr) {}

  uint32_t read_reg(uint32_t offset) { return base[offset / 4]; }

  void write_reg(uint32_t offset, uint32_t value) { base[offset / 4] = value; }

  bool is_valid() {
    uint32_t magic = read_reg(VIRTIO_MMIO_MAGIC_VALUE);
    return magic == 0x74726976; // "virt" in little-endian
  }

  void probe() {
    if (!is_valid()) {
      return;
    }

    uint32_t version = read_reg(VIRTIO_MMIO_VERSION);
    device_id = read_reg(VIRTIO_MMIO_DEVICE_ID);
    vendor_id = read_reg(VIRTIO_MMIO_VENDOR_ID);
    uint32_t features = read_reg(VIRTIO_MMIO_DEVICE_FEATURES);

    oprintf("VirtIO Device at 0x%lx:\n", (uintptr_t)base);
    oprintf("  Magic: 0x%x\n", read_reg(VIRTIO_MMIO_MAGIC_VALUE));
    oprintf("  Version: %u\n", version);
    oprintf("  Device ID: %u", device_id);

    // Print device type
    switch (device_id) {
    case VIRTIO_ID_GPU:
      oprintf(" (GPU)\n");
      break;
    case 1:
      oprintf(" (Network)\n");
      break;
    case 2:
      oprintf(" (Block)\n");
      break;
    default:
      oprintf(" (Unknown)\n");
      break;
    }

    oprintf("  Vendor ID: 0x%x\n", vendor_id);
    oprintf("  Features: 0x%x\n", features);
  }
};

class VirtQueue {
public:
  struct virtq_desc *desc;
  struct virtq_avail *avail;
  struct virtq_used *used;
  uint16_t last_used_idx;
  uint16_t queue_size;

  void init(PageAddr mem, uint16_t size) {
    queue_size = size;
    last_used_idx = 0;

    // Layout: descriptors, then avail ring, then used ring
    // VirtIO requires: desc (16-byte aligned), avail (2-byte), used (4-byte)
    desc = mem.as<struct virtq_desc>();
    avail = (struct virtq_avail *)(desc + size);

    // Calculate used ring address with proper 4-byte alignment
    uintptr_t used_addr = (uintptr_t)avail + sizeof(struct virtq_avail) + sizeof(uint16_t) * size;
    used_addr = (used_addr + 3) & ~3; // Round up to 4-byte boundary
    used = (struct virtq_used *)used_addr;

    // Zero out the queue
    omemset(desc, 0, sizeof(struct virtq_desc) * size);
    omemset(avail, 0, sizeof(struct virtq_avail) + sizeof(uint16_t) * size);
    omemset(used, 0, sizeof(struct virtq_used) + sizeof(struct virtq_used_elem) * size);
  }

  void add_buf(uint16_t desc_idx, PageAddr buf, uint32_t len, bool write) {
    // buf is already a physical address (PageAddr), use it directly
    desc[desc_idx].addr = (uint64_t)buf.raw();
    desc[desc_idx].len = len;
    desc[desc_idx].flags = write ? VIRTQ_DESC_F_WRITE : 0;
    desc[desc_idx].next = 0;
  }

  void submit(uint16_t desc_idx) {
    uint16_t idx = avail->idx % queue_size;
    avail->ring[idx] = desc_idx;
    avail->idx++;
  }

  bool has_used() { return last_used_idx != used->idx; }

  uint32_t get_used() {
    if (!has_used())
      return 0xFFFFFFFF;

    uint16_t idx = last_used_idx % queue_size;
    last_used_idx++;
    return used->ring[idx].id;
  }
};

class VirtIOGPU {
public:
  VirtIODevice dev;
  VirtQueue controlq;
  PageAddr framebuffer;
  PageAddr cmd_page;  // Reusable command page
  PageAddr resp_page; // Reusable response page
  uint32_t width;
  uint32_t height;

  VirtIOGPU() : dev(0), width(1024), height(600) {}
  VirtIOGPU(uintptr_t addr) : dev(addr), width(1024), height(600) {}

  bool init() {
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

  void draw_pixel(uint32_t x, uint32_t y, uint32_t color) {
    if (x >= width || y >= height)
      return;
    uint32_t *fb = framebuffer.as<uint32_t>();
    fb[y * width + x] = color;
  }

  void fill_rect(uint32_t x, uint32_t y, uint32_t w, uint32_t h, uint32_t color) {
    for (uint32_t dy = 0; dy < h; dy++) {
      for (uint32_t dx = 0; dx < w; dx++) {
        draw_pixel(x + dx, y + dy, color);
      }
    }
  }

  void flush() {
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
};

void scan_virtio_devices() {
  oprintf("Scanning for VirtIO devices...\n\n");

  for (int i = 0; i < VIRTIO_MMIO_COUNT; i++) {
    uintptr_t addr = VIRTIO_MMIO_BASE + (i * VIRTIO_MMIO_SIZE);
    VirtIODevice dev(addr);
    dev.probe();
  }
}

// Global GPU instance (to avoid dynamic allocation)
static VirtIOGPU g_gpu;

// Simple PRNG for static effect
static uint32_t rng_state = 0x12345678;
static uint32_t rand_u32() {
  // Xorshift32
  rng_state ^= rng_state << 13;
  rng_state ^= rng_state >> 17;
  rng_state ^= rng_state << 5;
  return rng_state;
}

void graphics_demo_main_proc() {
  oprintf("=== VirtIO GPU Graphics Demo ===\n");

  // Find and initialize GPU
  VirtIOGPU *gpu = nullptr;

  for (int i = 0; i < VIRTIO_MMIO_COUNT; i++) {
    uintptr_t addr = VIRTIO_MMIO_BASE + (i * VIRTIO_MMIO_SIZE);
    VirtIODevice test_dev(addr);
    test_dev.device_id = test_dev.read_reg(VIRTIO_MMIO_DEVICE_ID);

    if (test_dev.is_valid() && test_dev.device_id == VIRTIO_ID_GPU) {
      g_gpu = VirtIOGPU(addr);
      gpu = &g_gpu;
      break;
    }
  }

  if (!gpu) {
    oprintf("No VirtIO GPU found!\n");
    current_proc->state = TERMINATED;
    while (true) {
      yield();
    }
    return;
  }

  // Initialize the GPU
  if (!gpu->init()) {
    oprintf("Failed to initialize GPU\n");
    current_proc->state = TERMINATED;
    while (true) {
      yield();
    }
    return;
  }

  // Create framebuffer
  gpu->create_framebuffer();

  oprintf("Starting animated static effect...\n");

  // Animate static effect
  uint32_t *fb = gpu->framebuffer.as<uint32_t>();
  uint32_t frame = 0;
  while (true) {
    // Fill screen with random grayscale pixels
    for (uint32_t i = 0; i < gpu->width * gpu->height; i++) {
      uint8_t gray = rand_u32() & 0xFF;
      fb[i] = 0xFF000000 | (gray << 16) | (gray << 8) | gray; // BGRA: gray on all channels
    }

    // Flush to display
    gpu->flush();

    frame++;
    if (frame % 60 == 0) {
      oprintf("Frame %u\n", frame);
    }

    // Small delay to control frame rate
    for (volatile int i = 0; i < 10000; i++)
      ;
    oprintf("yielding\n");
    yield();
  }
}