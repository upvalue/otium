// form.hpp — shape accessors shared by every walker over source forms
// (the compiler's analysis and emit passes, the stage-0 expander, and the
// top-level control helpers). These are pure Value predicates: none of them
// allocate, so a raw Value may be passed straight in.
#pragma once
#include "heap.hpp"
#include "value.hpp"

namespace ot {

inline bool pairp(Value v) { return v.tag == Tag::Pair; }
inline Value car_(Value v) { return as_pair(v)->car; }
inline Value cdr_(Value v) { return as_pair(v)->cdr; }
inline bool sym_is(Value v, u32 name) { return v.tag == Tag::Symbol && v.id == name; }

// The reader lowers a bracket literal to `(array x y ...)`. Forms that accept
// either a bare list or a bracket literal strip the head first.
inline Value strip_array_literal_head(Value forms, u32 arrayId) {
  return pairp(forms) && sym_is(car_(forms), arrayId) ? cdr_(forms) : forms;
}

// A leading string documents the body only when at least one form follows it;
// a lone string is the body's value. Returns the body past the docstring and
// writes the docstring (or nil) through `doc` when non-null.
inline Value skip_docstring(Value rest, Value* doc = nullptr) {
  if (pairp(rest) && car_(rest).tag == Tag::String && pairp(cdr_(rest))) {
    if (doc) *doc = car_(rest);
    return cdr_(rest);
  }
  if (doc) *doc = nil_v();
  return rest;
}

}  // namespace ot
