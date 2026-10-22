/* Fixed-layout records. Declaration identity is maintained by the Otium prelude. */
static otv nat_make_record_type(ots* state, otv* args, int argc) {
  if (argc != 3 || !is_type(args[0], OBJ_SYMBOL) || !proper_list(args[1]) ||
      (args[2] != ot_true && args[2] != ot_false))
    return ot_raise(state, "defrecord: expected name, field list and boolean immutability");
  otv fields = args[1], keys = ot_nil, key = ot_nil;
  OT_FRAME_SCOPED(state, &fields, &keys, &key);
  keys = ot_array_new(state, list_length(fields));
  while (is_type(fields, OBJ_PAIR)) {
    otv field = as_pair(fields)->car;
    if (!is_type(field, OBJ_SYMBOL)) return ot_raise(state, "defrecord: expected field symbol");
    buf name = {0};
    buf_write(&name, as_name(field)->bytes, as_name(field)->length);
    if (name.failed) {
      buf_free(&name);
      return ot_raise(state, "defrecord: field name too large");
    }
    key = ot_intern(state, name.data, name.length, true);
    buf_free(&name);
    for (size_t i = 0; i < ot_array_length(keys); i++)
      if (ot_array_get(keys, i, ot_nil) == key)
        return ot_raise(state, "defrecord: duplicate field");
    ot_array_append(state, keys, key);
    fields = as_pair(fields)->cdr;
  }
  ot_record_type_obj* type = must_alloc(state, sizeof(*type), OBJ_RECORD_TYPE);
  ot_store(state, type, &type->name, args[0]);
  ot_store(state, type, &type->keys, keys);
  type->immutable = args[2] == ot_true;
  type->stable_id = ++state->next_stable_id;
  return ot_from_obj(type);
}

static otv nat_record_new(ots* state, otv* args, int argc) {
  if (argc < 1 || !is_type(args[0], OBJ_RECORD_TYPE))
    return ot_raise(state, "record-new: expected record type");
  size_t count = ot_array_length(as_record_type(args[0])->keys);
  if ((size_t)(argc - 1) != count) return ot_raise(state, "record-new: wrong field count");
  ot_record_obj* record = must_alloc(state, sizeof(*record) + count * sizeof(otv), OBJ_RECORD);
  ot_store(state, record, &record->type, args[0]);
  record->stable_id = ++state->next_stable_id;
  for (size_t i = 0; i < count; i++) ot_store(state, record, &record->values[i], args[i + 1]);
  return ot_from_obj(record);
}

static size_t record_index(otv record, otv key) {
  otv keys = as_record_type(as_record(record)->type)->keys;
  for (size_t i = 0; i < ot_array_length(keys); i++)
    if (ot_array_get(keys, i, ot_nil) == key) return i;
  return SIZE_MAX;
}

static otv record_read(ots* state, otv record, otv key) {
  if (!is_type(record, OBJ_RECORD)) return ot_raise(state, "record-ref: expected record");
  size_t index = record_index(record, key);
  if (index == SIZE_MAX) return ot_raise(state, "record-ref: unknown record field");
  return as_record(record)->values[index];
}

static otv record_write(ots* state, otv record, otv key, otv value) {
  if (!is_type(record, OBJ_RECORD)) return ot_raise(state, "record-set!: expected record");
  if (as_record_type(as_record(record)->type)->immutable)
    return ot_raise(state, "record-set!: immutable record");
  size_t index = record_index(record, key);
  if (index == SIZE_MAX) return ot_raise(state, "record-set!: unknown record field");
  ot_record_obj* object = as_record(record);
  ot_store(state, object, &object->values[index], value);
  return value;
}

static otv nat_record_ref(ots* state, otv* args, int argc) {
  if (argc != 2) return ot_raise(state, "record-ref: expected record and key");
  return record_read(state, args[0], args[1]);
}

static otv nat_record_set(ots* state, otv* args, int argc) {
  if (argc != 3) return ot_raise(state, "record-set!: expected record, key and value");
  return record_write(state, args[0], args[1], args[2]);
}

static otv nat_record_p(ots* state, otv* args, int argc) {
  if (argc != 1) return ot_raise(state, "record?: expected one value");
  return is_type(args[0], OBJ_RECORD) ? ot_true : ot_false;
}

static otv nat_record_type_p(ots* state, otv* args, int argc) {
  if (argc != 1) return ot_raise(state, "record-type?: expected one value");
  return is_type(args[0], OBJ_RECORD_TYPE) ? ot_true : ot_false;
}

static otv nat_record_type(ots* state, otv* args, int argc) {
  if (argc != 1 || !is_type(args[0], OBJ_RECORD))
    return ot_raise(state, "record-type: expected record");
  return as_record(args[0])->type;
}
