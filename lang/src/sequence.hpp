// Rooted iteration over the sequence kinds shared by native consumers.
#pragma once
#include "state.hpp"

namespace ot {

enum class SeqStep : u8 { Item, End, Improper, NotSequence };

// The caller owns both slots for the iterator's lifetime. Keeping the cursor
// and current item on the VM stack makes them safe across a moving collection.
class SeqIter {
public:
  explicit SeqIter(Slot rootedSequence) : cursor_(rootedSequence) {
    Value seq = cursor_.get();
    if (seq.tag == Tag::Array) {
      kind_ = Kind::Array;
      limit_ = as_array(seq)->len;
    } else if (seq.tag == Tag::Pair || seq.tag == Tag::Null) {
      kind_ = Kind::List;
    } else if (is_nil(seq)) {
      kind_ = Kind::Empty;
    }
  }

  SeqStep next(Slot item) {
    switch (kind_) {
      case Kind::Empty: return SeqStep::End;
      case Kind::Invalid: return SeqStep::NotSequence;
      case Kind::Array: {
        if (index_ >= limit_) return SeqStep::End;
        ArrayData* array = as_array(cursor_.get());
        // for-each historically snapshots the length and uses get for each
        // index, so shrinking during a callback yields nil for removed slots.
        item.set(index_ < array->len ? array->items[index_] : nil_v());
        index_++;
        return SeqStep::Item;
      }
      case Kind::List: {
        Value cursor = cursor_.get();
        if (cursor.tag == Tag::Null) return SeqStep::End;
        if (cursor.tag != Tag::Pair) return SeqStep::Improper;
        PairData* pair = as_pair(cursor);
        item.set(pair->car);
        cursor_.set(pair->cdr);
        return SeqStep::Item;
      }
    }
    return SeqStep::NotSequence;
  }

private:
  enum class Kind : u8 { Invalid, Empty, List, Array };

  Slot cursor_;
  u32 index_ = 0;
  u32 limit_ = 0;
  Kind kind_ = Kind::Invalid;
};

inline Value sequence_error(State& vm, const char* who, SeqStep step) {
  if (step == SeqStep::Improper) return raise_error(vm, "%s: improper list", who);
  return raise_error(vm, "%s: expected sequence", who);
}

}  // namespace ot
