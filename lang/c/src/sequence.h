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

// The caller owns both slots for the iterator's lifetime. Keeping the cursor
// and current item on the VM stack makes them safe across a moving collection.
typedef struct SeqIter {
  Slot cursor;
  u32 index;
  u32 limit;
  SeqKind kind;
} SeqIter;

static inline void seq_iter_init(SeqIter* it, Slot rootedSequence) {
  it->cursor = rootedSequence;
  it->index = 0;
  it->limit = 0;
  it->kind = SeqKind_Invalid;
  Value seq = slot_get(it->cursor);
  if (seq.tag == Tag_Array) {
    it->kind = SeqKind_Array;
    it->limit = as_array(seq)->len;
  } else if (seq.tag == Tag_Pair || seq.tag == Tag_Null) {
    it->kind = SeqKind_List;
  } else if (is_nil(seq)) {
    it->kind = SeqKind_Empty;
  }
}

static inline SeqStep seq_iter_next(SeqIter* it, Slot item) {
  switch (it->kind) {
    case SeqKind_Empty: return SeqStep_End;
    case SeqKind_Invalid: return SeqStep_NotSequence;
    case SeqKind_Array: {
      if (it->index >= it->limit) return SeqStep_End;
      ArrayData* array = as_array(slot_get(it->cursor));
      // for-each historically snapshots the length and uses get for each
      // index, so shrinking during a callback yields nil for removed slots.
      slot_set(item, it->index < array->len ? array->items[it->index] : nil_v());
      it->index++;
      return SeqStep_Item;
    }
    case SeqKind_List: {
      Value cursor = slot_get(it->cursor);
      if (cursor.tag == Tag_Null) return SeqStep_End;
      if (cursor.tag != Tag_Pair) return SeqStep_Improper;
      PairData* pair = as_pair(cursor);
      slot_set(item, pair->car);
      slot_set(it->cursor, pair->cdr);
      return SeqStep_Item;
    }
  }
  return SeqStep_NotSequence;
}

static inline Value sequence_error(State* vm, const char* who, SeqStep step) {
  if (step == SeqStep_Improper) return raise_error(vm, "%s: improper list", who);
  return raise_error(vm, "%s: expected sequence", who);
}
