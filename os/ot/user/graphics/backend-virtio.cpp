#include "ot/user/graphics/backend-virtio.hpp"
#include "ot/lib/logger.hpp"

bool VirtioGraphicsBackend::init() {
  if (!dev.is_valid()) {
    l.log("GPU: Device not valid");
    return false;
  }

  // Read device ID
  dev.device_id = dev.read_reg(VIRTIO_MMIO_DEVICE_ID);
  if (dev.device_id != VIRTIO_ID_GPU) {
    l.log("GPU: Not a GPU device (id=%u)", dev.device_id);
    return false;
  }

  l.log("Initializing VirtIO GPU...");

  if (!dev.init()) {
    l.log("GPU: Feature negotiation failed");
    return false;
  }

  // Check queue availability
  dev.write_reg(VIRTIO_MMIO_QUEUE_SEL, 0);
  uint32_t max_queue_size = dev.read_reg(VIRTIO_MMIO_QUEUE_NUM_MAX);
  l.log("Queue 0 max size: %u", max_queue_size);
  if (max_queue_size == 0) {
    l.log("GPU: Queue 0 not available");
    return false;
  }
  if (QUEUE_SIZE > max_queue_size) {
    l.log("GPU: QUEUE_SIZE (%u) > max (%u)", QUEUE_SIZE, max_queue_size);
    return false;
  }

  // Setup queue (2 pages for legacy VirtIO - used ring must be page-aligned)
  PageAddr queue_mem = PageAddr((uintptr_t)ou_alloc_page());
  ou_alloc_page(); // Second page for used ring
  dev.setup_queue(0, controlq, queue_mem, QUEUE_SIZE);
  l.log("Queue physical addr: 0x%x", queue_mem.raw());

  dev.set_driver_ok();
  l.log("Status after DRIVER_OK: 0x%x", dev.read_reg(VIRTIO_MMIO_STATUS));

  l.log("GPU: Initialization complete");

  // Create framebuffer
  create_framebuffer();

  return true;
}

uint32_t VirtioGraphicsBackend::send_command(PageAddr cmd, uint32_t cmd_len, PageAddr resp, uint32_t resp_len) {
  // Zero out response buffer
  memset(resp.as_ptr(), 0, resp_len);

  // Chain command (out) -> response (in) and submit
  controlq.chain().out(cmd, cmd_len).in(resp, resp_len).submit();

  // oprintf("  avail idx: %u->%u, used idx: %u\n", avail_idx_before, controlq.avail->idx, used_idx_before);
  // Dump raw descriptor bytes
  uint8_t *desc0_bytes = (uint8_t *)&controlq.desc[0];
  // oprintf("  desc[0] raw bytes: ");
  for (int i = 0; i < 16; i++) {
    // oprintf("%02x ", desc0_bytes[i]);
  }
  // oprintf("\n");
  /*oprintf("  desc[0]: addr=0x%llx, len=%u, flags=0x%x, next=%u\n", controlq.desc[0].addr, controlq.desc[0].len,
          controlq.desc[0].flags, controlq.desc[0].next);
  oprintf("  avail->ring[0]=%u\n", controlq.avail->ring[0]);*/

  // Notify device
  dev.write_reg(VIRTIO_MMIO_QUEUE_NOTIFY, 0);

  // Wait for response (simple polling)
  uint32_t timeout = 1000000;
  while (!controlq.has_used() && timeout-- > 0) {
    // Busy wait
  }

  if (timeout == 0) {
    l.log("GPU: Command timeout! used idx still: %u", controlq.used->idx);
    return 0xFFFFFFFF;
  }

  // oprintf("  Response received, used idx: %u\n", controlq.used->idx);
  controlq.get_used();

  // Return response type
  struct virtio_gpu_ctrl_hdr *resp_hdr = resp.as<struct virtio_gpu_ctrl_hdr>();
  // oprintf("  Response type: 0x%x, flags: 0x%x\n", resp_hdr->type, resp_hdr->flags);
  return resp_hdr->type;
}

void VirtioGraphicsBackend::create_framebuffer() {
  l.log("Creating framebuffer (%ux%u)...", width, height);

  // Allocate framebuffer memory (need multiple contiguous pages)
  uint32_t fb_size = width * height * 4; // 4 bytes per pixel (BGRA)
  uint32_t fb_pages = (fb_size + OT_PAGE_SIZE - 1) / OT_PAGE_SIZE;

  // Lock known framebuffer memory (guaranteed contiguous)
  void *fb_ptr = ou_lock_known_memory(KNOWN_MEMORY_FRAMEBUFFER, fb_pages);
  if (!fb_ptr) {
    l.log("ERROR: Failed to lock framebuffer memory (%u pages)", fb_pages);
    return;
  }
  framebuffer = PageAddr((uintptr_t)fb_ptr);

  l.log("Locked %u contiguous pages for framebuffer at 0x%x", fb_pages, framebuffer.raw());

  // Allocate command/response pages (reused for all commands)
  cmd_page = PageAddr((uintptr_t)ou_alloc_page());
  resp_page = PageAddr((uintptr_t)ou_alloc_page());

  l.log("CMD page: 0x%x, RESP page: 0x%x", cmd_page.raw(), resp_page.raw());

  struct virtio_gpu_resource_create_2d *cmd = cmd_page.as<struct virtio_gpu_resource_create_2d>();
  memset(cmd, 0, sizeof(*cmd));
  cmd->hdr.type = VIRTIO_GPU_CMD_RESOURCE_CREATE_2D;
  cmd->hdr.flags = 0;
  cmd->hdr.fence_id = 0;
  cmd->hdr.ctx_id = 0;
  cmd->hdr.padding = 0;
  cmd->resource_id = 1;
  cmd->format = VIRTIO_GPU_FORMAT_B8G8R8A8_UNORM;
  cmd->width = width;
  cmd->height = height;

  l.log("Sending CREATE_2D: res_id=%u, fmt=%u, %ux%u", cmd->resource_id, cmd->format, cmd->width, cmd->height);

  uint32_t resp_type = send_command(cmd_page, sizeof(*cmd), resp_page, sizeof(struct virtio_gpu_ctrl_hdr));
  l.log("Resource create response: 0x%x", resp_type);
  if (resp_type != VIRTIO_GPU_RESP_OK_NODATA) {
    l.log("ERROR: Resource creation failed!");
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
  l.log("Attach backing response: 0x%x", resp_type);
  if (resp_type != VIRTIO_GPU_RESP_OK_NODATA) {
    l.log("ERROR: Attach backing failed!");
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
  l.log("Set scanout response: 0x%x", resp_type);
  if (resp_type != VIRTIO_GPU_RESP_OK_NODATA) {
    l.log("ERROR: Set scanout failed!");
    return;
  }

  l.log("Framebuffer setup complete, ready for drawing");
}

void VirtioGraphicsBackend::flush() {
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
  // l.log("Transfer response: 0x%x", resp_type);
  if (resp_type != VIRTIO_GPU_RESP_OK_NODATA) {
    // This seems to happen occasionally
    // oprintf("ERROR: Transfer failed!\n");
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
  // oprintf("Flush response: 0x%x\n", resp_type);
  if (resp_type != VIRTIO_GPU_RESP_OK_NODATA) {
    // oprintf("ERROR: Flush failed!\n");
    return;
  }
}
