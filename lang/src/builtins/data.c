// builtins/data.c — pair, list, array, and table natives. Spec 10.3, 10.5.
// The collection machinery itself lives in src/collections.c; everything here
// is written against slots.h and holds no heap value outside a rooted slot.
#include "../builtins.h"

static Value nat_cons(State* vm, u32 base, u32 argc) {
  OT_TRY(need_argc(vm, "cons", argc, 2, 2));
  OT_SCOPE(vm);
  Ref out = ot_push(vm);
  ot_cons(vm, out, ARG(0), ARG(1));
  return ot_ret(vm, out);
}

static Value nat_car(State* vm, u32 base, u32 argc) {
  OT_TRY(need_argc(vm, "car", argc, 1, 1));
  OT_TRY(need_pair(vm, "car", ARG(0)));
  ot_car(vm, ARG(0), ARG(0));  // arg slots are scratch once checked
  return ot_ret(vm, ARG(0));
}
static Value nat_cdr(State* vm, u32 base, u32 argc) {
  OT_TRY(need_argc(vm, "cdr", argc, 1, 1));
  OT_TRY(need_pair(vm, "cdr", ARG(0)));
  ot_cdr(vm, ARG(0), ARG(0));
  return ot_ret(vm, ARG(0));
}
static Value set_pair_field(State* vm, u32 base, u32 argc, const char* who, bool car) {
  OT_TRY(need_argc(vm, who, argc, 2, 2));
  OT_TRY(need_pair(vm, who, ARG(0)));
  if (ot_pair_key_frozen(vm, ARG(0)))
    return raise_error(vm, "%s: cannot mutate a pair used as a table key", who);
  if (ot_pair_contains(vm, ARG(1), ARG(0)))
    return raise_error(vm, "%s: cyclic pair structure is not supported", who);
  ot_pair_set(vm, ARG(0), car, ARG(1));
  return ot_ret(vm, ARG(0));
}
static Value nat_set_car(State* vm, u32 base, u32 argc) {
  return set_pair_field(vm, base, argc, "set-car!", true);
}
static Value nat_set_cdr(State* vm, u32 base, u32 argc) {
  return set_pair_field(vm, base, argc, "set-cdr!", false);
}

// caar/cadr/cddr: first step into the pair, checking at each level.
static Value two_step(State* vm, u32 base, u32 argc, const char* who, bool firstCar,
                      bool secondCar) {
  OT_TRY(need_argc(vm, who, argc, 1, 1));
  OT_TRY(need_pair(vm, who, ARG(0)));
  if (firstCar) ot_car(vm, ARG(0), ARG(0));
  else ot_cdr(vm, ARG(0), ARG(0));
  OT_TRY(need_pair(vm, who, ARG(0)));
  if (secondCar) ot_car(vm, ARG(0), ARG(0));
  else ot_cdr(vm, ARG(0), ARG(0));
  return ot_ret(vm, ARG(0));
}
static Value nat_caar(State* vm, u32 base, u32 argc) {
  return two_step(vm, base, argc, "caar", true, true);
}
static Value nat_cadr(State* vm, u32 base, u32 argc) {
  return two_step(vm, base, argc, "cadr", false, true);
}
static Value nat_cddr(State* vm, u32 base, u32 argc) {
  return two_step(vm, base, argc, "cddr", false, false);
}

static Value nat_list(State* vm, u32 base, u32 argc) {
  OT_SCOPE(vm);
  Ref tail = ot_push(vm);
  ot_set_null(vm, tail);
  Ref out = ot_push(vm);
  ot_list_from_stack(vm, out, base, argc, tail);  // args are contiguous at base
  return ot_ret(vm, out);
}

static Value nat_append(State* vm, u32 base, u32 argc) {
  OT_SCOPE(vm);
  Ref acc = ot_push(vm);
  ot_set_null(vm, acc);
  Ref cursor = ot_push(vm);
  for (u32 i = argc; i-- > 0;) {
    Tag t = ot_tag(vm, ARG(i));
    if (t != Tag_Null && t != Tag_Pair) return raise_error(vm, "append: expected proper list");
    // Spread the i-th list onto rooted scratch slots, then fold onto acc.
    ot_copy(vm, cursor, ARG(i));
    u32 ebase = ot_top(vm);
    while (ot_tag(vm, cursor) != Tag_Null) {
      if (ot_tag(vm, cursor) != Tag_Pair) return raise_error(vm, "append: improper list");
      Ref elem = ot_push(vm);
      ot_car(vm, elem, cursor);
      ot_cdr(vm, cursor, cursor);
    }
    ot_list_from_stack(vm, acc, ebase, ot_top(vm) - ebase, acc);
    ot_pop_to(vm, ebase);
  }
  return ot_ret(vm, acc);
}

static Value nat_length(State* vm, u32 base, u32 argc) {
  OT_TRY(need_argc(vm, "length", argc, 1, 1));
  switch (ot_tag(vm, ARG(0))) {
    case Tag_Null: return int_v(0);
    case Tag_Pair: {
      OT_SCOPE(vm);
      Ref p = ot_push_copy(vm, ARG(0));
      i64 n = 0;
      while (ot_tag(vm, p) != Tag_Null) {
        if (ot_tag(vm, p) != Tag_Pair) return raise_error(vm, "length: improper list");
        ot_cdr(vm, p, p);
        n++;
      }
      return int_v(n);
    }
    case Tag_Array: return int_v((i64)ot_array_len(vm, ARG(0)));
    case Tag_Table: return int_v((i64)ot_table_count(vm, ARG(0)));
    case Tag_String: return int_v((i64)ot_string_nchars(vm, ARG(0)));
    case Tag_Buffer: return int_v((i64)ot_buffer_nchars(vm, ARG(0)));
    default: return raise_error(vm, "length: unsupported type");
  }
}

static Value nat_reverse(State* vm, u32 base, u32 argc) {
  OT_TRY(need_argc(vm, "reverse", argc, 1, 1));
  Tag t = ot_tag(vm, ARG(0));
  if (t == Tag_Nil) return nil_v();  // kind-preserving: nothing to preserve
  if (t == Tag_Null) return null_v();
  if (t == Tag_Pair) {
    OT_SCOPE(vm);
    Ref acc = ot_push(vm);
    ot_set_null(vm, acc);
    Ref p = ot_push_copy(vm, ARG(0));
    Ref head = ot_push(vm);
    while (ot_tag(vm, p) != Tag_Null) {
      if (ot_tag(vm, p) != Tag_Pair) return raise_error(vm, "reverse: improper list");
      ot_car(vm, head, p);
      ot_cons(vm, acc, head, acc);
      ot_cdr(vm, p, p);
    }
    return ot_ret(vm, acc);
  }
  if (t == Tag_Array) {
    OT_SCOPE(vm);
    Ref out = ot_push(vm);
    ot_make_array(vm, out, ot_array_len(vm, ARG(0)));
    Ref item = ot_push(vm);
    for (u32 i = ot_array_len(vm, ARG(0)); i-- > 0;) {
      ot_array_get(vm, item, ARG(0), i);
      ot_array_push(vm, out, item);
    }
    return ot_ret(vm, out);
  }
  return raise_error(vm, "reverse: expected sequence");
}

static Value nat_list_to_array(State* vm, u32 base, u32 argc) {
  OT_TRY(need_argc(vm, "list->array", argc, 1, 1));
  Tag t = ot_tag(vm, ARG(0));
  if (t != Tag_Null && t != Tag_Pair) return raise_error(vm, "list->array: expected list");
  OT_SCOPE(vm);
  Ref out = ot_push(vm);
  ot_make_array(vm, out, 8);
  Ref p = ot_push_copy(vm, ARG(0));
  Ref head = ot_push(vm);
  while (ot_tag(vm, p) != Tag_Null) {
    if (ot_tag(vm, p) != Tag_Pair) return raise_error(vm, "list->array: improper list");
    ot_car(vm, head, p);
    ot_array_push(vm, out, head);
    ot_cdr(vm, p, p);
  }
  return ot_ret(vm, out);
}

static Value nat_array_to_list(State* vm, u32 base, u32 argc) {
  OT_TRY(need_argc(vm, "array->list", argc, 1, 1));
  OT_TRY(need_array(vm, "array->list", ARG(0)));
  OT_SCOPE(vm);
  Ref acc = ot_push(vm);
  ot_set_null(vm, acc);
  Ref item = ot_push(vm);
  for (u32 i = ot_array_len(vm, ARG(0)); i-- > 0;) {
    ot_array_get(vm, item, ARG(0), i);
    ot_cons(vm, acc, item, acc);
  }
  return ot_ret(vm, acc);
}

static Value nat_array(State* vm, u32 base, u32 argc) {
  OT_SCOPE(vm);
  Ref out = ot_push(vm);
  ot_make_array(vm, out, argc ? argc : 4);
  for (u32 i = 0; i < argc; i++) ot_array_push(vm, out, ARG(i));
  return ot_ret(vm, out);
}

static Value nat_table(State* vm, u32 base, u32 argc) {
  if (argc % 2 != 0) return raise_error(vm, "table: odd argument count");
  OT_SCOPE(vm);
  Ref t = ot_push(vm);
  ot_make_table(vm, t);
  for (u32 i = 0; i < argc; i += 2) ot_table_put(vm, t, ARG(i), ARG(i + 1));
  return ot_ret(vm, t);
}

static Value nat_get(State* vm, u32 base, u32 argc) {
  OT_TRY(need_argc(vm, "get", argc, 2, 3));
  OT_SCOPE(vm);
  Ref dflt = argc == 3 ? ARG(2) : ot_push(vm);
  Ref dst = ot_push(vm);
  OT_TRY(ot_collection_get(vm, dst, ARG(0), ARG(1), dflt));
  return ot_ret(vm, dst);
}

static Value nat_put(State* vm, u32 base, u32 argc) {
  OT_TRY(need_argc(vm, "put!", argc, 3, UINT32_MAX));
  if ((argc - 1) % 2 != 0) return raise_error(vm, "put!: expected coll plus key/value pairs");
  for (u32 i = 1; i < argc; i += 2)
    OT_TRY(ot_collection_put(vm, ARG(0), ARG(i), ARG(i + 1)));
  return ot_ret(vm, ARG(0));
}

static Value nat_push(State* vm, u32 base, u32 argc) {
  OT_TRY(need_argc(vm, "push!", argc, 1, UINT32_MAX));
  OT_TRY(ot_array_push_args(vm, ARG(0), base + 1, argc - 1));
  return ot_ret(vm, ARG(0));
}

static Value nat_pop(State* vm, u32 base, u32 argc) {
  OT_TRY(need_argc(vm, "pop!", argc, 1, 1));
  OT_TRY(need_array(vm, "pop!", ARG(0)));
  OT_SCOPE(vm);
  Ref out = ot_push(vm);
  ot_array_pop(vm, out, ARG(0));
  return ot_ret(vm, out);
}

static Value nat_update(State* vm, u32 base, u32 argc) {
  OT_TRY(need_argc(vm, "update!", argc, 3, UINT32_MAX));
  OT_SCOPE(vm);
  Ref dflt = ot_push(vm);
  Ref cur = ot_push(vm);
  OT_TRY(ot_collection_get(vm, cur, ARG(0), ARG(1), dflt));
  Ref result = ot_push(vm);
  u32 cbase = ot_top(vm);
  ot_push_copy(vm, cur);
  for (u32 i = 3; i < argc; i++) ot_push_copy(vm, ARG(i));
  OT_TRY(ot_apply(vm, result, ARG(2), cbase, argc - 2));
  OT_TRY(ot_collection_put(vm, ARG(0), ARG(1), result));
  return ot_ret(vm, ARG(0));
}

static Value keys_or_values(State* vm, u32 base, u32 argc, const char* who, bool wantKeys) {
  OT_TRY(need_argc(vm, who, argc, 1, 1));
  OT_SCOPE(vm);
  Ref out = ot_push(vm);
  ot_make_array(vm, out, 8);
  if (ot_nil(vm, ARG(0))) return ot_ret(vm, out);
  if (ot_tag(vm, ARG(0)) != Tag_Table) return raise_error(vm, "%s: expected table", who);
  Ref k = ot_push(vm);
  Ref v = ot_push(vm);
  u32 cursor = 0;
  while (ot_table_next(vm, ARG(0), &cursor, k, v))
    ot_array_push(vm, out, wantKeys ? k : v);
  return ot_ret(vm, out);
}

static Value nat_keys(State* vm, u32 base, u32 argc) {
  return keys_or_values(vm, base, argc, "keys", true);
}

static Value nat_values(State* vm, u32 base, u32 argc) {
  return keys_or_values(vm, base, argc, "values", false);
}

static Value nat_copy(State* vm, u32 base, u32 argc) {
  OT_TRY(need_argc(vm, "copy", argc, 1, 1));
  Tag t = ot_tag(vm, ARG(0));
  if (t == Tag_Nil) return nil_v();  // kind-preserving over absence
  if (t == Tag_Array) {
    OT_SCOPE(vm);
    Ref out = ot_push(vm);
    ot_make_array(vm, out, ot_array_len(vm, ARG(0)));
    Ref item = ot_push(vm);
    for (u32 i = 0; i < ot_array_len(vm, ARG(0)); i++) {
      ot_array_get(vm, item, ARG(0), i);
      ot_array_push(vm, out, item);
    }
    return ot_ret(vm, out);
  }
  if (t == Tag_Table) {
    OT_SCOPE(vm);
    Ref out = ot_push(vm);
    ot_make_table(vm, out);
    Ref k = ot_push(vm);
    Ref v = ot_push(vm);
    u32 cursor = 0;
    while (ot_table_next(vm, ARG(0), &cursor, k, v)) ot_table_put(vm, out, k, v);
    return ot_ret(vm, out);
  }
  return raise_error(vm, "copy: expected array, table, or nil");
}

void register_data(State* vm) {
  ot_def_native(vm, "cons", nat_cons);
  ot_def_native(vm, "car", nat_car);
  ot_def_native(vm, "cdr", nat_cdr);
  ot_def_native(vm, "set-car!", nat_set_car);
  ot_def_native(vm, "set-cdr!", nat_set_cdr);
  ot_def_native(vm, "caar", nat_caar);
  ot_def_native(vm, "cadr", nat_cadr);
  ot_def_native(vm, "cddr", nat_cddr);
  ot_def_native(vm, "list", nat_list);
  ot_def_native(vm, "append", nat_append);
  ot_def_native(vm, "length", nat_length);
  ot_def_native(vm, "reverse", nat_reverse);
  ot_def_native(vm, "list->array", nat_list_to_array);
  ot_def_native(vm, "array->list", nat_array_to_list);
  ot_def_native(vm, "array", nat_array);
  ot_def_native(vm, "table", nat_table);
  ot_def_native(vm, "get", nat_get);
  ot_def_native(vm, "put!", nat_put);
  ot_def_native(vm, "push!", nat_push);
  ot_def_native(vm, "pop!", nat_pop);
  ot_def_native(vm, "update!", nat_update);
  ot_def_native(vm, "keys", nat_keys);
  ot_def_native(vm, "values", nat_values);
  ot_def_native(vm, "copy", nat_copy);
}
