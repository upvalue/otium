// foreign.c — extension-facing foreign userdata helpers.
#include "heap.h"
#include "state.h"

static u32 foreign_payload_bytes(u32 payloadBytes) {
  if (payloadBytes > UINT32_MAX - (u32)sizeof(ForeignData))
    ot_fatal("foreign: payload size overflow");
  return (u32)sizeof(ForeignData) + payloadBytes;
}

u32 register_foreign_type(State* vm, const char* name, ForeignFinalizer finalize) {
  u32 nameSym = intern_id(&vm->intern, name, (u32)strlen(name));
  return heap_add_foreign_type(&vm->heap, nameSym, finalize);
}

Value make_foreign_inline(State* vm, u32 typeId, const void* payload, u32 payloadBytes) {
  if (!heap_foreign_type(&vm->heap, typeId)) ot_fatal("foreign: invalid type id");
  if (payloadBytes && !payload) ot_fatal("foreign: null inline payload");
  Obj* o = heap_alloc(&vm->heap, ObjType_Foreign, foreign_payload_bytes(payloadBytes));
  ForeignData* d = (ForeignData*)obj_payload(o);
  d->typeId = typeId;
  d->flags = 0;
  d->payloadSize = payloadBytes;
  if (payloadBytes) memcpy(d + 1, payload, payloadBytes);
  return obj_v(Tag_Foreign, o);
}

Value make_foreign_pointer(State* vm, u32 typeId, void* payload) {
  if (!heap_foreign_type(&vm->heap, typeId)) ot_fatal("foreign: invalid type id");
  Obj* o = heap_alloc(&vm->heap, ObjType_Foreign, foreign_payload_bytes(sizeof payload));
  ForeignData* d = (ForeignData*)obj_payload(o);
  d->typeId = typeId;
  d->flags = ForeignExternal;
  d->payloadSize = sizeof payload;
  memcpy(d + 1, &payload, sizeof payload);
  return obj_v(Tag_Foreign, o);
}

static const char* foreign_type_name(State* vm, u32 typeId, u32* len) {
  const ForeignType* type = heap_foreign_type(&vm->heap, typeId);
  if (!type) {
    *len = 7;
    return "foreign";
  }
  const char* name = intern_name(&vm->intern, type->nameSym, len);
  if (!name) {
    *len = 7;
    return "foreign";
  }
  return name;
}

Value foreign_check(State* vm, const char* who, Value value, u32 expectedType, void** out) {
  u32 len = 0;
  const char* name = foreign_type_name(vm, expectedType, &len);
  if (value.tag != Tag_Foreign) return raise_error(vm, "%s: expected %.*s", who, (int)len, name);
  ForeignData* d = as_foreign(value);
  if (d->typeId != expectedType) return raise_error(vm, "%s: expected %.*s", who, (int)len, name);
  if (d->flags & ForeignDead)
    return raise_error(vm, "%s: %.*s has been released", who, (int)len, name);
  if (d->flags & ForeignExternal) {
    memcpy(out, d + 1, sizeof *out);
  } else {
    *out = d + 1;
  }
  return nil_v();
}

Value foreign_release(State* vm, const char* who, Value value, u32 expectedType) {
  void* payload = nullptr;
  OT_TRY(foreign_check(vm, who, value, expectedType, &payload));
  (void)payload;
  heap_finalize_foreign(&vm->heap, value.obj);
  return nil_v();
}
