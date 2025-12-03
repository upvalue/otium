#ifndef OT_USER_VIRTIO_DEBUG_HPP
#define OT_USER_VIRTIO_DEBUG_HPP

#include "ot/user/virtio/virtio.hpp"
#include "ot/lib/logger.hpp"

namespace virtio_debug {

inline void dump_descriptor(const struct virtq_desc &desc, uint16_t idx, const char *prefix = "") {
  oprintf("%sdesc[%u]: addr=0x%llx, len=%u, flags=0x%04x",
          prefix, idx, desc.addr, desc.len, desc.flags);

  // Decode flags
  bool has_next = desc.flags & VIRTQ_DESC_F_NEXT;
  bool is_write = desc.flags & VIRTQ_DESC_F_WRITE;

  oprintf(" [%s%s]",
          is_write ? "WRITE" : "READ",
          has_next ? ", NEXT" : "");

  if (has_next) {
    oprintf(" next=%u", desc.next);
  }
  oprintf("\n");
}

inline void dump_queue(const VirtQueue &q, const char *name = "VirtQueue") {
  oprintf("=== %s Debug Dump ===\n", name);
  oprintf("Queue size: %u\n", q.queue_size);
  oprintf("Available ring: idx=%u, flags=0x%04x\n", q.avail->idx, q.avail->flags);
  oprintf("Used ring: idx=%u, flags=0x%04x\n", q.used->idx, q.used->flags);
  oprintf("Last used idx (driver): %u\n", q.last_used_idx);

  // Show which descriptors are in the available ring
  oprintf("\nAvailable ring entries:\n");
  uint16_t avail_count = q.avail->idx;
  if (avail_count == 0) {
    oprintf("  (empty)\n");
  } else {
    uint16_t start = avail_count > 8 ? avail_count - 8 : 0;
    for (uint16_t i = start; i < avail_count; i++) {
      uint16_t ring_idx = i % q.queue_size;
      uint16_t desc_idx = q.avail->ring[ring_idx];
      oprintf("  avail[%u] -> desc %u%s\n",
              i, desc_idx,
              i >= q.last_used_idx ? " (pending)" : " (processed)");
    }
  }

  // Show used ring entries
  oprintf("\nUsed ring entries:\n");
  uint16_t used_count = q.used->idx;
  if (used_count == 0) {
    oprintf("  (empty)\n");
  } else {
    uint16_t start = used_count > 8 ? used_count - 8 : 0;
    for (uint16_t i = start; i < used_count; i++) {
      uint16_t ring_idx = i % q.queue_size;
      oprintf("  used[%u]: id=%u, len=%u%s\n",
              i,
              q.used->ring[ring_idx].id,
              q.used->ring[ring_idx].len,
              i < q.last_used_idx ? " (consumed)" : " (available)");
    }
  }

  // Show all descriptors
  oprintf("\nDescriptor table:\n");
  for (uint16_t i = 0; i < q.queue_size; i++) {
    dump_descriptor(q.desc[i], i, "  ");
  }

  oprintf("=== End %s Dump ===\n\n", name);
}

inline void dump_queue_state(const VirtQueue &q, const char *label) {
  oprintf("[%s] avail.idx=%u, used.idx=%u, last_used=%u, pending=%u\n",
          label, q.avail->idx, q.used->idx, q.last_used_idx,
          (uint16_t)(q.avail->idx - q.last_used_idx));
}

} // namespace virtio_debug

#endif
