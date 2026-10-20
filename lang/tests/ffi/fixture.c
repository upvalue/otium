#include <pthread.h>
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>
#define ECHO(NAME, TYPE)                                                                           \
  TYPE echo_##NAME(TYPE x) { return x; }
// clang-format off
ECHO(i8, int8_t)
ECHO(u8, uint8_t)
ECHO(i16, int16_t)
ECHO(u16, uint16_t)
ECHO(i32, int32_t)
ECHO(u32, uint32_t)
ECHO(i64, int64_t)
ECHO(u64, uint64_t)
ECHO(short, short)
ECHO(ushort, unsigned short)
ECHO(int, int)
ECHO(uint, unsigned int)
ECHO(long, long)
ECHO(ulong, unsigned long)
ECHO(size_t, size_t)
ECHO(ptrdiff_t, ptrdiff_t)
ECHO(float, float)
ECHO(double, double)
ECHO(bool, bool)
ECHO(pointer, void*)
// clang-format on
const char* echo_utf8(const char* text) { return text; }
const char* bad_utf8(void) { return "\xc0\xaf"; }
const char* empty_utf8(void) { return ""; }
static int count;
uint64_t high_u64(void) {
  count++;
  return UINT64_MAX;
}
int calls(void) { return count; }
void write_high(uint64_t* p) { *p = UINT64_MAX; }
bool null_callback(void (*callback)(void)) { return callback == NULL; }
void pointer_out(void** out, void* value) { *out = value; }
static const char* retained;
void retain(const char* text) { retained = text; }
const char* retained_text(void) { return retained; }
static char scratch[32] = "first";
const char* changing_text(void) { return scratch; }
void change_text(void) { strcpy(scratch, "second"); }
#ifndef FIXTURE_ID
#define FIXTURE_ID 17
#endif
int fixture_id(void) { return FIXTURE_ID; }
typedef struct {
  double x, y;
} Point;
Point translate(Point p, double x, double y) {
  p.x += x;
  p.y += y;
  return p;
}
typedef struct {
  uint8_t tag;
  double value;
} Mixed;
Mixed mixed(Mixed x) {
  x.tag++;
  x.value *= 2;
  return x;
}
typedef struct {
  Point points[3];
  uint64_t tag;
} Large;
Large large(Large x) {
  x.points[2].y += 10;
  x.tag++;
  return x;
}

typedef union {
  double number;
  uint64_t integer;
} Number;
Number number_add(Number n, double amount) {
  n.number += amount;
  return n;
}
typedef union {
  float one;
  float many[3];
} Floats;
Floats floats_add(Floats n, float amount) {
  n.many[2] += amount;
  return n;
}
typedef union {
  double one;
  double many[4];
} Doubles;
Doubles doubles_add(Doubles n, double amount) {
  n.many[3] += amount;
  return n;
}
typedef union {
  double one;
  double many[5];
} BigUnion;
BigUnion big_union_add(BigUnion n, double amount) {
  n.many[4] += amount;
  return n;
}
typedef union {
  float single;
  double number;
} MixedFloat;
MixedFloat mixed_float_add(MixedFloat n, double amount) {
  n.number += amount;
  return n;
}
typedef union {
  struct {
    uint32_t tag;
    float value;
  } pair;
  uint32_t tag;
} Half;
typedef struct {
  uint32_t prefix;
  Half middle;
  float suffix;
} OffsetUnion;
OffsetUnion offset_union_add(OffsetUnion n, float amount) {
  n.middle.pair.value += amount;
  n.suffix += 2 * amount;
  return n;
}
typedef struct {
  Number value;
  double suffix;
} NestedUnion;
NestedUnion nested_union_add(NestedUnion n, double amount) {
  n.value.number += amount;
  n.suffix += 2 * amount;
  return n;
}
typedef union {
  uint8_t byte;
  uint16_t word;
} TinyUnion;
TinyUnion tiny_union_add(TinyUnion n, uint16_t amount) {
  n.word += amount;
  return n;
}
typedef struct {
  Number values[2];
  uint64_t tag;
} UnionArray;
UnionArray union_array_add(UnionArray n, double amount) {
  n.values[1].number += amount;
  n.tag += 1;
  return n;
}
Doubles union_fp_pressure(double a, double b, double c, double d, double e, double f, double g,
                          Doubles value, double tail) {
  value.many[0] += a + b + c + d + e + f + g + tail;
  value.many[3] += 1;
  return value;
}
Number union_int_pressure(uint64_t a, uint64_t b, uint64_t c, uint64_t d, uint64_t e, uint64_t f,
                          uint64_t g, uint64_t h, Number value, uint64_t tail) {
  value.integer += a + b + c + d + e + f + g + h + tail;
  return value;
}
Number high_union(void) {
  Number n;
  n.integer = UINT64_MAX;
  return n;
}
uint8_t union_high_byte(Number n) { return (uint8_t)(n.integer >> 56); }
/* Compiler-computed layout data is independent of the Otium declarations. */
size_t union_layout(int which) {
  switch (which) {
    case 0: return sizeof(Number);
    case 1: return _Alignof(Number);
    case 2: return sizeof(Floats);
    case 3: return _Alignof(Floats);
    case 4: return sizeof(Doubles);
    case 5: return _Alignof(Doubles);
    case 6: return sizeof(OffsetUnion);
    case 7: return offsetof(OffsetUnion, middle);
    case 8: return offsetof(OffsetUnion, suffix);
    case 9: return sizeof(UnionArray);
    case 10: return offsetof(UnionArray, tag);
    default: return 0;
  }
}

/* Callbacks: repeated calls, a stored callback called by a later foreign call,
 * a callback run while a string argument must stay valid, and a wrong thread. */
int call_n(int (*f)(int), int n) {
  int sum = 0;
  for (int i = 0; i < n; i++) sum += f(i);
  return sum;
}
static int (*stored)(int);
void store_callback(int (*f)(int)) { stored = f; }
int call_stored(int x) { return stored ? stored(x) : -1; }
double call_mixed(double (*f)(double, int64_t, void*, bool), void* p) {
  return f(1.5, -3, p, true);
}
bool call_predicate(bool (*f)(uint8_t), uint8_t x) { return f(x); }
void* call_pointer(void* (*f)(void*), void* p) { return f(p); }
const char* text_after(const char* text, void (*f)(void)) {
  f();
  return text;
}
static void (*threaded)(void);
static void* thread_main(void* ignored) {
  (void)ignored;
  threaded();
  return NULL;
}
/* Lets the reload check write a module without runtime-specific file APIs. */
int write_file(const char* path, const char* text) {
  FILE* file = fopen(path, "w");
  if (!file) return -1;
  int status = fputs(text, file) < 0 ? -1 : 0;
  return fclose(file) ? -1 : status;
}
void call_on_thread(void (*f)(void)) {
  pthread_t thread;
  threaded = f;
  pthread_create(&thread, NULL, thread_main, NULL);
  pthread_join(thread, NULL);
}
