typedef struct ot_string_obj {
  long length;
} ot_string_obj;

#define OT_GC_MAY_ALLOCATE __attribute__((annotate("otium.gc.may_allocate")))
OT_GC_MAY_ALLOCATE void grow_left(void);
OT_GC_MAY_ALLOCATE void grow_right(void);
int choice(void);
void consume(long length);

// Two invalidation origins circulate through overlapping cycles. Keeping the
// first predecessor's origin oscillates forever with this CFG ordering.
void cycles(long value) {
  ot_string_obj* string = (ot_string_obj*)value;
  goto use;
use:
  consume(string->length);
  if (choice()) goto left;
  else goto branch;
left:
  grow_left();
  if (choice()) goto back;
  else goto left;
right:
  grow_right();
  if (choice()) goto join;
  else goto back;
join:
  if (choice()) goto use;
  else goto left;
branch:
  if (choice()) goto right;
  else goto back;
back:
  if (choice()) goto use;
  else goto branch;
}

// At the join, mixed is valid on one path and stale on the other. The next
// safepoint must retain both possible origins, while a reset clears both.
void mixed_paths(long value) {
  ot_string_obj* mixed = (ot_string_obj*)value;
  ot_string_obj* refreshed = (ot_string_obj*)value;
  if (choice()) grow_left();
  grow_right();
  refreshed = (ot_string_obj*)value;
  consume(mixed->length);
  consume(refreshed->length);
}
