#include "vec.h"

static void* default_alloc(void* ud, size_t n) {
  (void)ud;
  return malloc(n);
}
static void* default_realloc(void* ud, void* p, size_t n) {
  (void)ud;
  return realloc(p, n);
}
static void default_free(void* ud, void* p) {
  (void)ud;
  free(p);
}

static OtAllocator g_alloc = {default_alloc, default_realloc, default_free, nullptr};

void ot_set_allocator(const OtAllocator* a) {
  if (a)
    g_alloc = *a;
  else
    g_alloc = (OtAllocator){default_alloc, default_realloc, default_free, nullptr};
}
void* ot_alloc(size_t n) { return g_alloc.alloc(g_alloc.ud, n); }
void* ot_realloc(void* p, size_t n) { return g_alloc.realloc(g_alloc.ud, p, n); }
void ot_free(void* p) { g_alloc.free(g_alloc.ud, p); }

void vecraw_reserve(VecRaw* v, u32 n, size_t elemSize) {
  if (n <= v->cap) return;
  u32 ncap = grow_capacity(v->cap, n, "Vec: capacity overflow");
  void* nd = ot_realloc(v->data, (size_t)ncap * elemSize);
  if (!nd) ot_fatal("Vec: out of memory");
  v->data = nd;
  v->cap = ncap;
}

void vecraw_deinit(VecRaw* v) {
  ot_free(v->data);
  v->data = nullptr;
  v->len = 0;
  v->cap = 0;
}

void buf_append(Buf* b, const char* s, u32 n) {
  if (!n) return;
  vec_reserve(b, b->len + n);
  memcpy(b->data + b->len, s, n);
  b->len += n;
}

void buf_append_cstr(Buf* b, const char* s) { buf_append(b, s, (u32)strlen(s)); }

void buf_printf(Buf* b, const char* fmt, ...) {
  va_list ap;
  va_start(ap, fmt);
  va_list ap2;
  va_copy(ap2, ap);
  int need = vsnprintf(nullptr, 0, fmt, ap);
  va_end(ap);
  if (need > 0) {
    vec_reserve(b, b->len + (u32)need + 1);
    vsnprintf(b->data + b->len, (size_t)need + 1, fmt, ap2);
    b->len += (u32)need;
  }
  va_end(ap2);
}
