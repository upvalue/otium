#ifndef OT_GC_INTERNAL_INC
#define OT_GC_INTERNAL_INC

#include "otium.h"

static inline size_t ot_gc_align_object(size_t size) { return (size + 7u) & ~(size_t)7u; }

static inline uintptr_t ot_gc_make_header(ot_obj_type type, size_t size) {
  return ((uintptr_t)size << 8u) | ((uintptr_t)type << 1u);
}

void ot_gc_finalize_ext(ots* state, ot_ext_obj* ext);
void ot_gc_record_phase(ot_gc_phase_stats* phase, uint64_t elapsed_ns);

bool ot_gc_heap_init(ots* state);
void ot_gc_heap_destroy(ots* state);

#ifdef OT_GC_GEN
void ot_gc_write_barrier(ots* state, const ot_obj* owner, otv value);
#else
static inline void ot_gc_write_barrier(ots* state, const ot_obj* owner, otv value) {
  (void)state;
  (void)owner;
  (void)value;
}
#endif

static inline void ot_store(ots* state, const void* owner, otv* slot, otv value) {
  *slot = value;
  ot_gc_write_barrier(state, owner, value);
}

#endif
