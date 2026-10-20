typedef unsigned long otv;
typedef struct ot_state ots;

typedef struct ot_pair_obj {
  unsigned long header;
  otv car;
  otv cdr;
  unsigned long stable_id;
} ot_pair_obj;

typedef struct ot_slots_obj {
  unsigned long header;
  unsigned long capacity;
  otv values[];
} ot_slots_obj;

typedef struct ot_entry {
  otv key;
  otv value;
  unsigned hash;
} ot_entry;

typedef struct ot_entries_obj {
  unsigned long header;
  unsigned long capacity;
  ot_entry values[];
} ot_entries_obj;

typedef struct ot_bytes_obj {
  unsigned long header;
  unsigned long length;
  unsigned char bytes[];
} ot_bytes_obj;

struct ot_state {
  otv root;
};

typedef struct native_frame {
  otv value;
} native_frame;

#define OT_GC_ALLOW_DIRECT_STORE(REASON)                                                           \
  __attribute__((annotate("otium.gc.allow_direct_store:" REASON)))

static void ot_store(ots* state, const void* owner, otv* slot, otv value) {
  (void)state;
  (void)owner;
  *slot = value;
}

void* memcpy(void* destination, const void* source, unsigned long size);

void safe(ots* state, ot_pair_obj* pair, ot_slots_obj* slots, ot_entries_obj* entries,
          native_frame* frame, otv value) {
  ot_store(state, pair, &pair->car, value);
  ot_store(state, slots, &slots->values[0], value);
  ot_store(state, entries, &entries->values[0].key, value);
  otv* slot = &pair->cdr;
  slot = &pair->car;
  ot_store(state, pair, slot, value);
  pair->stable_id = 1;
  state->root = value;
  frame->value = value;
}

void safe_bulk(ot_bytes_obj* bytes, const ot_bytes_obj* source) {
  memcpy(bytes, source, sizeof(*bytes));
}

void unsafe_pair(ot_pair_obj* pair, otv value) { pair->car = value; }

void unsafe_slots(ot_slots_obj* slots, otv value) { slots->values[0] = value; }

void unsafe_entry(ot_entries_obj* entries, otv value) {
  ot_entry* entry = &entries->values[0];
  entry->value = value;
}

void unsafe_alias(ot_pair_obj* pair, otv value) {
  otv* slot = &pair->cdr;
  *slot = value;
}

void unsafe_owner(ots* state, ot_pair_obj* pair, ot_entries_obj* entries, otv value) {
  ot_store(state, pair, &entries->values[0].value, value);
}

void unsafe_target(ots* state, ot_pair_obj* pair, otv value) {
  ot_store(state, pair, &state->root, value);
}

void unsafe_record(ot_pair_obj* pair, otv value) {
  *pair = (ot_pair_obj){.car = value, .cdr = value};
}

void unsafe_bulk(ot_pair_obj* pair, const ot_pair_obj* source) {
  memcpy(pair, source, sizeof(*pair));
}

OT_GC_ALLOW_DIRECT_STORE("object is outside the managed heap")

void escaped(ot_pair_obj* pair, otv value) { pair->car = value; }

OT_GC_ALLOW_DIRECT_STORE("")

void empty_escape(ot_pair_obj* pair, otv value) { pair->car = value; }
