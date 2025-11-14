// Minimal VirtIO GPU driver for RISC-V bare metal
// This implements basic 2D operations for displaying graphics

#include <stdint.h>

typedef unsigned long size_t;

// UART for debug output
#define UART_BASE 0x10000000
volatile uint8_t* uart = (uint8_t*)UART_BASE;

void putchar(char c) {
    *uart = c;
}

void print(const char* str) {
    while (*str) putchar(*str++);
}

void print_hex(uint32_t val) {
    const char* hex = "0123456789ABCDEF";
    print("0x");
    for (int i = 7; i >= 0; i--) {
        putchar(hex[(val >> (i * 4)) & 0xF]);
    }
}

// VirtIO MMIO registers offsets
#define VIRTIO_MMIO_MAGIC           0x000
#define VIRTIO_MMIO_VERSION         0x004
#define VIRTIO_MMIO_DEVICE_ID       0x008
#define VIRTIO_MMIO_VENDOR_ID       0x00c
#define VIRTIO_MMIO_DEVICE_FEATURES 0x010
#define VIRTIO_MMIO_DRIVER_FEATURES 0x020
#define VIRTIO_MMIO_QUEUE_SEL       0x030
#define VIRTIO_MMIO_QUEUE_NUM_MAX   0x034
#define VIRTIO_MMIO_QUEUE_NUM       0x038
#define VIRTIO_MMIO_QUEUE_READY     0x044
#define VIRTIO_MMIO_QUEUE_NOTIFY    0x050
#define VIRTIO_MMIO_STATUS          0x070
#define VIRTIO_MMIO_QUEUE_DESC_LOW  0x080
#define VIRTIO_MMIO_QUEUE_DESC_HIGH 0x084
#define VIRTIO_MMIO_QUEUE_AVAIL_LOW 0x090
#define VIRTIO_MMIO_QUEUE_AVAIL_HIGH 0x094
#define VIRTIO_MMIO_QUEUE_USED_LOW  0x0a0
#define VIRTIO_MMIO_QUEUE_USED_HIGH 0x0a4

// VirtIO status flags
#define VIRTIO_STATUS_ACKNOWLEDGE    1
#define VIRTIO_STATUS_DRIVER         2
#define VIRTIO_STATUS_DRIVER_OK      4
#define VIRTIO_STATUS_FEATURES_OK    8
#define VIRTIO_STATUS_FAILED         128

// VirtIO GPU specific
#define VIRTIO_GPU_DEVICE_ID         16  // GPU device ID

// VirtIO GPU commands
#define VIRTIO_GPU_CMD_GET_DISPLAY_INFO     0x0100
#define VIRTIO_GPU_CMD_RESOURCE_CREATE_2D   0x0101
#define VIRTIO_GPU_CMD_RESOURCE_UNREF       0x0102
#define VIRTIO_GPU_CMD_SET_SCANOUT          0x0103
#define VIRTIO_GPU_CMD_RESOURCE_FLUSH       0x0104
#define VIRTIO_GPU_CMD_TRANSFER_TO_HOST_2D  0x0105
#define VIRTIO_GPU_CMD_RESOURCE_ATTACH_BACKING 0x0106

// VirtIO GPU formats
#define VIRTIO_GPU_FORMAT_B8G8R8A8_UNORM    1  // BGRA format

// Simple memory operations
extern "C" void* memset(void* dst, int value, size_t count) {
    uint8_t* d = (uint8_t*)dst;
    uint8_t v = (uint8_t)value;
    for (size_t i = 0; i < count; i++) {
        d[i] = v;
    }
    return dst;
}

// VirtIO Queue descriptor
struct virtq_desc {
    uint64_t addr;
    uint32_t len;
    uint16_t flags;
    uint16_t next;
} __attribute__((packed));

// VirtIO Queue available ring
struct virtq_avail {
    uint16_t flags;
    uint16_t idx;
    uint16_t ring[256];
} __attribute__((packed));

// VirtIO Queue used ring
struct virtq_used {
    uint16_t flags;
    uint16_t idx;
    struct {
        uint32_t id;
        uint32_t len;
    } ring[256];
} __attribute__((packed));

// VirtIO GPU control header
struct virtio_gpu_ctrl_hdr {
    uint32_t type;
    uint32_t flags;
    uint64_t fence_id;
    uint32_t ctx_id;
    uint32_t padding;
} __attribute__((packed));

// Resource create 2D command
struct virtio_gpu_resource_create_2d {
    struct virtio_gpu_ctrl_hdr hdr;
    uint32_t resource_id;
    uint32_t format;
    uint32_t width;
    uint32_t height;
} __attribute__((packed));

// Resource attach backing
struct virtio_gpu_mem_entry {
    uint64_t addr;
    uint32_t length;
    uint32_t padding;
} __attribute__((packed));

struct virtio_gpu_resource_attach_backing {
    struct virtio_gpu_ctrl_hdr hdr;
    uint32_t resource_id;
    uint32_t nr_entries;
    struct virtio_gpu_mem_entry entries[1];
} __attribute__((packed));

// Set scanout command
struct virtio_gpu_rect {
    uint32_t x;
    uint32_t y;
    uint32_t width;
    uint32_t height;
} __attribute__((packed));

struct virtio_gpu_set_scanout {
    struct virtio_gpu_ctrl_hdr hdr;
    struct virtio_gpu_rect r;
    uint32_t scanout_id;
    uint32_t resource_id;
} __attribute__((packed));

// Transfer to host 2D
struct virtio_gpu_transfer_to_host_2d {
    struct virtio_gpu_ctrl_hdr hdr;
    struct virtio_gpu_rect r;
    uint64_t offset;
    uint32_t resource_id;
    uint32_t padding;
} __attribute__((packed));

// Resource flush
struct virtio_gpu_resource_flush {
    struct virtio_gpu_ctrl_hdr hdr;
    struct virtio_gpu_rect r;
    uint32_t resource_id;
    uint32_t padding;
} __attribute__((packed));

// Global virtio-gpu state
struct {
    volatile uint32_t* base;
    struct virtq_desc* desc;
    struct virtq_avail* avail;
    struct virtq_used* used;
    uint16_t last_used_idx;
    uint16_t next_avail_idx;
    uint32_t* framebuffer;
} gpu;

// Align to page boundary
#define ALIGN_PAGE(x) (((x) + 0xFFF) & ~0xFFF)

// Initialize a virtqueue
void setup_virtqueue(int queue_id, void* desc_addr) {
    // Select queue
    gpu.base[VIRTIO_MMIO_QUEUE_SEL/4] = queue_id;

    // Get queue size
    uint32_t queue_size = gpu.base[VIRTIO_MMIO_QUEUE_NUM_MAX/4];
    print("Queue "); print_hex(queue_id);
    print(" size: "); print_hex(queue_size); print("\n");

    if (queue_size == 0) return;

    // Set queue size
    gpu.base[VIRTIO_MMIO_QUEUE_NUM/4] = queue_size;

    // Calculate queue component addresses
    gpu.desc = (struct virtq_desc*)desc_addr;
    gpu.avail = (struct virtq_avail*)((uint8_t*)desc_addr + queue_size * sizeof(struct virtq_desc));
    gpu.used = (struct virtq_used*)ALIGN_PAGE((uintptr_t)gpu.avail + 4 + queue_size * 2);

    // Clear the queue memory
    memset(desc_addr, 0, 0x4000);

    // Set queue addresses
    uint64_t desc_paddr = (uint64_t)(uintptr_t)gpu.desc;
    uint64_t avail_paddr = (uint64_t)(uintptr_t)gpu.avail;
    uint64_t used_paddr = (uint64_t)(uintptr_t)gpu.used;

    gpu.base[VIRTIO_MMIO_QUEUE_DESC_LOW/4] = desc_paddr & 0xFFFFFFFF;
    gpu.base[VIRTIO_MMIO_QUEUE_DESC_HIGH/4] = desc_paddr >> 32;
    gpu.base[VIRTIO_MMIO_QUEUE_AVAIL_LOW/4] = avail_paddr & 0xFFFFFFFF;
    gpu.base[VIRTIO_MMIO_QUEUE_AVAIL_HIGH/4] = avail_paddr >> 32;
    gpu.base[VIRTIO_MMIO_QUEUE_USED_LOW/4] = used_paddr & 0xFFFFFFFF;
    gpu.base[VIRTIO_MMIO_QUEUE_USED_HIGH/4] = used_paddr >> 32;

    // Enable queue
    gpu.base[VIRTIO_MMIO_QUEUE_READY/4] = 1;
}

// Send a command to the GPU
void send_gpu_command(void* cmd, uint32_t cmd_size, void* resp, uint32_t resp_size) {
    uint16_t idx = gpu.next_avail_idx;

    // Setup descriptor for command
    gpu.desc[idx % 256].addr = (uint64_t)(uintptr_t)cmd;
    gpu.desc[idx % 256].len = cmd_size;
    gpu.desc[idx % 256].flags = 1; // NEXT
    gpu.desc[idx % 256].next = (idx + 1) % 256;

    // Setup descriptor for response
    gpu.desc[(idx + 1) % 256].addr = (uint64_t)(uintptr_t)resp;
    gpu.desc[(idx + 1) % 256].len = resp_size;
    gpu.desc[(idx + 1) % 256].flags = 2; // WRITE
    gpu.desc[(idx + 1) % 256].next = 0;

    // Add to available ring
    gpu.avail->ring[gpu.avail->idx % 256] = idx % 256;
    __asm__ volatile("fence w, w" ::: "memory");
    gpu.avail->idx++;

    // Notify device
    gpu.base[VIRTIO_MMIO_QUEUE_NOTIFY/4] = 0;

    gpu.next_avail_idx = (idx + 2) % 256;

    // Wait for completion (polling)
    while (gpu.used->idx == gpu.last_used_idx) {
        __asm__ volatile("" ::: "memory");
    }
    gpu.last_used_idx = gpu.used->idx;
}

// Initialize virtio-gpu device
int init_virtio_gpu(uint32_t base_addr) {
    gpu.base = (volatile uint32_t*)base_addr;

    // Check magic number
    uint32_t magic = gpu.base[VIRTIO_MMIO_MAGIC/4];
    if (magic != 0x74726976) { // "virt" in little-endian
        print("Invalid magic: "); print_hex(magic); print("\n");
        return -1;
    }

    // Check device ID
    uint32_t device_id = gpu.base[VIRTIO_MMIO_DEVICE_ID/4];
    if (device_id != VIRTIO_GPU_DEVICE_ID) {
        print("Not a GPU device: "); print_hex(device_id); print("\n");
        return -1;
    }

    print("Found VirtIO GPU at "); print_hex(base_addr); print("\n");

    // Reset device
    gpu.base[VIRTIO_MMIO_STATUS/4] = 0;

    // Set ACKNOWLEDGE
    gpu.base[VIRTIO_MMIO_STATUS/4] = VIRTIO_STATUS_ACKNOWLEDGE;

    // Set DRIVER
    gpu.base[VIRTIO_MMIO_STATUS/4] |= VIRTIO_STATUS_DRIVER;

    // Negotiate features (we don't need any specific features for basic 2D)
    uint32_t features = gpu.base[VIRTIO_MMIO_DEVICE_FEATURES/4];
    print("Device features: "); print_hex(features); print("\n");
    gpu.base[VIRTIO_MMIO_DRIVER_FEATURES/4] = 0;

    // Set FEATURES_OK
    gpu.base[VIRTIO_MMIO_STATUS/4] |= VIRTIO_STATUS_FEATURES_OK;

    // Setup virtqueues (queue 0 is control queue)
    static uint8_t queue_mem[0x10000] __attribute__((aligned(4096)));
    setup_virtqueue(0, queue_mem);

    // Set DRIVER_OK
    gpu.base[VIRTIO_MMIO_STATUS/4] |= VIRTIO_STATUS_DRIVER_OK;

    print("VirtIO GPU initialized\n");
    return 0;
}

// Create a framebuffer and display it
void create_framebuffer() {
    static uint32_t fb[640 * 480] __attribute__((aligned(4096)));
    gpu.framebuffer = fb;

    // Create 2D resource
    struct virtio_gpu_resource_create_2d create_cmd = {
        .hdr = { .type = VIRTIO_GPU_CMD_RESOURCE_CREATE_2D },
        .resource_id = 1,
        .format = VIRTIO_GPU_FORMAT_B8G8R8A8_UNORM,
        .width = 640,
        .height = 480
    };
    struct virtio_gpu_ctrl_hdr create_resp = {};

    print("Creating 2D resource...\n");
    send_gpu_command(&create_cmd, sizeof(create_cmd), &create_resp, sizeof(create_resp));

    // Attach backing memory
    struct virtio_gpu_resource_attach_backing attach_cmd = {
        .hdr = { .type = VIRTIO_GPU_CMD_RESOURCE_ATTACH_BACKING },
        .resource_id = 1,
        .nr_entries = 1,
        .entries = {{ .addr = (uint64_t)(uintptr_t)fb, .length = 640 * 480 * 4 }}
    };
    struct virtio_gpu_ctrl_hdr attach_resp = {};

    print("Attaching backing memory...\n");
    send_gpu_command(&attach_cmd, sizeof(attach_cmd), &attach_resp, sizeof(attach_resp));

    // Set scanout
    struct virtio_gpu_set_scanout scanout_cmd = {
        .hdr = { .type = VIRTIO_GPU_CMD_SET_SCANOUT },
        .r = { .x = 0, .y = 0, .width = 640, .height = 480 },
        .scanout_id = 0,
        .resource_id = 1
    };
    struct virtio_gpu_ctrl_hdr scanout_resp = {};

    print("Setting scanout...\n");
    send_gpu_command(&scanout_cmd, sizeof(scanout_cmd), &scanout_resp, sizeof(scanout_resp));
}

// Draw some test patterns
void draw_test_pattern() {
    if (!gpu.framebuffer) return;

    // Fill with blue background
    for (int i = 0; i < 640 * 480; i++) {
        gpu.framebuffer[i] = 0xFF400000; // BGRA: Blue background
    }

    // Draw red rectangle
    for (int y = 50; y < 150; y++) {
        for (int x = 50; x < 150; x++) {
            gpu.framebuffer[y * 640 + x] = 0xFF0000FF; // Red
        }
    }

    // Draw green rectangle
    for (int y = 50; y < 150; y++) {
        for (int x = 200; x < 300; x++) {
            gpu.framebuffer[y * 640 + x] = 0xFF00FF00; // Green
        }
    }

    // Draw white rectangle
    for (int y = 50; y < 150; y++) {
        for (int x = 350; x < 450; x++) {
            gpu.framebuffer[y * 640 + x] = 0xFFFFFFFF; // White
        }
    }

    // Transfer to host
    struct virtio_gpu_transfer_to_host_2d transfer_cmd = {
        .hdr = { .type = VIRTIO_GPU_CMD_TRANSFER_TO_HOST_2D },
        .r = { .x = 0, .y = 0, .width = 640, .height = 480 },
        .offset = 0,
        .resource_id = 1
    };
    struct virtio_gpu_ctrl_hdr transfer_resp = {};

    print("Transferring to host...\n");
    send_gpu_command(&transfer_cmd, sizeof(transfer_cmd), &transfer_resp, sizeof(transfer_resp));

    // Flush to display
    struct virtio_gpu_resource_flush flush_cmd = {
        .hdr = { .type = VIRTIO_GPU_CMD_RESOURCE_FLUSH },
        .r = { .x = 0, .y = 0, .width = 640, .height = 480 },
        .resource_id = 1
    };
    struct virtio_gpu_ctrl_hdr flush_resp = {};

    print("Flushing display...\n");
    send_gpu_command(&flush_cmd, sizeof(flush_cmd), &flush_resp, sizeof(flush_resp));
}

// Scan for VirtIO devices
void scan_virtio_devices() {
    // VirtIO MMIO devices on virt machine are typically at:
    // 0x10001000, 0x10002000, ..., 0x10008000
    for (uint32_t addr = 0x10001000; addr <= 0x10008000; addr += 0x1000) {
        volatile uint32_t* mmio = (volatile uint32_t*)addr;
        uint32_t magic = mmio[VIRTIO_MMIO_MAGIC/4];

        if (magic == 0x74726976) { // "virt"
            uint32_t device_id = mmio[VIRTIO_MMIO_DEVICE_ID/4];
            print("Found VirtIO device at "); print_hex(addr);
            print(" ID: "); print_hex(device_id); print("\n");

            if (device_id == VIRTIO_GPU_DEVICE_ID) {
                if (init_virtio_gpu(addr) == 0) {
                    return;
                }
            }
        }
    }
    print("No VirtIO GPU found!\n");
}

extern "C" void _start() {
    // Set up stack
    __asm__ volatile("la sp, __stack_top");

    print("\n=== VirtIO GPU Demo ===\n");

    // Find and initialize VirtIO GPU
    scan_virtio_devices();

    // Create framebuffer
    create_framebuffer();

    // Draw test pattern
    draw_test_pattern();

    print("Done! Graphics should be visible.\n");

    // Infinite loop
    while (1) {
        __asm__ volatile("wfi");
    }
}