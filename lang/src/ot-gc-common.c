#define OT_INTERNAL
#include "otium.h"

#include <stdlib.h>

#include "ot-gc-internal.inc"

void ot_frame_push(ots* state, ot_frame* frame, otv** slots, size_t count) {
  frame->prev = state->frames;
  frame->state = state;
  frame->count = count;
  frame->slots = slots;
  state->frames = frame;
}

void ot_frame_pop(ots* state, ot_frame* frame) {
  if (state->frames != frame) {
    fputs("otium: root frame pop out of order\n", stderr);
    abort();
  }
  state->frames = frame->prev;
  frame->state = NULL;
}

void ot_frame_cleanup(ot_frame* frame) {
  if (frame->state != NULL) ot_frame_pop(frame->state, frame);
}

void ot_global_add(ots* state, otv* slot) {
  ot_global_root* root = ot_host_alloc(sizeof(*root));
  if (root == NULL) abort();
  root->next = state->globals;
  root->slot = slot;
  state->globals = root;
}

void ot_gc_finalize_ext(ots* state, ot_ext_obj* ext) {
  if (ext->released || !ext->pointer_payload || ext->payload.pointer == NULL) return;
  if (ext->type == 0 || ext->type > state->ext_type_count) return;
  ot_ext_finalizer finalizer = state->ext_types[ext->type - 1].finalizer;
  if (finalizer != NULL) finalizer(state, ext->payload.pointer);
}

void ot_gc_record_phase(ot_gc_phase_stats* phase, uint64_t elapsed_ns) {
  phase->collections++;
  phase->total_pause_ns += elapsed_ns;
  if (elapsed_ns > phase->max_pause_ns) phase->max_pause_ns = elapsed_ns;
}

void ot_reset_gc_stats(ots* state) {
  ot_gc_stats current = ot_get_gc_stats(state);
  state->stats = (ot_gc_stats){
      .peak_used_bytes = current.used_bytes,
      .reserved_bytes = current.reserved_bytes,
      .metadata_bytes = current.metadata_bytes,
  };
}
