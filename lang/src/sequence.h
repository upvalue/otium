// Rooted iteration over the sequence kinds shared by native consumers.
#pragma once
#include "state.h"

typedef enum SeqStep : u8 {
  SeqStep_Item,
  SeqStep_End,
  SeqStep_Improper,
  SeqStep_NotSequence,
} SeqStep;

typedef enum SeqKind : u8 {
  SeqKind_Invalid,
  SeqKind_Empty,
  SeqKind_List,
  SeqKind_Array,
} SeqKind;

// The caller owns both handles for the iterator's lifetime. Keeping the cursor
// and current item on the VM stack makes them safe across a moving collection.
// Ref is a bare index, so the iterator carries the State it indexes into.
typedef struct SeqIter {
  State* vm;
  Ref cursor;
  u32 index;
  u32 limit;
  SeqKind kind;
} SeqIter;

static inline void seq_iter_init(SeqIter* it, State* vm, Ref rootedSequence) {
  it->vm = vm;
  it->cursor = rootedSequence;
  it->index = 0;
  it->limit = 0;
  it->kind = SeqKind_Invalid;
  Value seq = ref_get(vm, it->cursor);
  if (seq.tag == Tag_Array) {
    it->kind = SeqKind_Array;
    it->limit = as_array(seq)->len;
  } else if (seq.tag == Tag_Pair || seq.tag == Tag_Null) {
    it->kind = SeqKind_List;
  } else if (is_nil(seq)) {
    it->kind = SeqKind_Empty;
  }
}

static inline SeqStep seq_iter_next(SeqIter* it, Ref item) {
  switch (it->kind) {
    case SeqKind_Empty: return SeqStep_End;
    case SeqKind_Invalid: return SeqStep_NotSequence;
    case SeqKind_Array: {
      if (it->index >= it->limit) return SeqStep_End;
      // Re-derived from the rooted cursor on every step: a callback between
      // steps can allocate and move the backing store.
      ArrayData* array = as_array(ref_get(it->vm, it->cursor));
      // for-each historically snapshots the length and uses get for each
      // index, so shrinking during a callback yields nil for removed slots.
      ref_set(it->vm, item, it->index < array->len ? array->items[it->index] : nil_v());
      it->index++;
      return SeqStep_Item;
    }
    case SeqKind_List: {
      Value cursor = ref_get(it->vm, it->cursor);
      if (cursor.tag == Tag_Null) return SeqStep_End;
      if (cursor.tag != Tag_Pair) return SeqStep_Improper;
      PairData* pair = as_pair(cursor);
      ref_set(it->vm, item, pair->car);
      ref_set(it->vm, it->cursor, pair->cdr);
      return SeqStep_Item;
    }
  }
  return SeqStep_NotSequence;
}

static inline Value sequence_error(State* vm, const char* who, SeqStep step) {
  if (step == SeqStep_Improper) return raise_error(vm, "%s: improper list", who);
  return raise_error(vm, "%s: expected sequence", who);
}
