#include "internal.h"

/* Native code, for x86-64 and arm64. The compiler, written in Otium, hands
 * over a unit: an instruction stream close to sljit's own operations, with
 * registers already allocated, plus tables of literals, global cells,
 * functions and frame descriptors. This file turns units into machine code,
 * keeps the code objects, and provides what that code calls: the entry
 * trampoline, stubs for every callee that isn't a closure, the unwinder, and
 * helpers for allocation, slow paths and control forms. See
 * scratch/native-compiler-design.md, sections 6 to 9. What depends on the
 * machine is in native-x86-64.inc and native-arm64.inc.
 *
 * Registers, in sljit's names: S0 holds the state and S1 the allocation
 * pointer, heap.start + heap.used, which is stored back before every call
 * into C. Allocation compares a bumped S1 with heap.end. At a call R0 holds
 * the argument count, R1 the callee and R2 to R6 the first five arguments;
 * the rest are in native.args from index 5. The result comes back in R0.
 * Every register but S0 and S1 is clobbered by a call.
 *
 * Frames are sljit's, holding the return address, any registers its
 * prologue saved, and the locals, from SLJIT_LOCALS_OFFSET above the stack
 * pointer, which hold spilled values and registers saved around slow paths.
 * Each call site has a descriptor, found by its return address, that gives
 * the frame size and the slots holding live values. Where the return address
 * is depends on the machine (return_slot); a call into C always leaves it
 * at sp[-1]. */

#ifdef OTIUM_NATIVE

#include "sljit-config.h"
#include "sljitLir.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <unistd.h>

/* The stream format. Integers are unsigned or signed LEB128. */
enum {
  NS_END,
  NS_FUNC,     /* f */
  NS_ENTER,    /* scratches slots */
  NS_LABEL,    /* label */
  NS_MOV,      /* mode dst src */
  NS_OP2,      /* op flags dst a b */
  NS_OP2U,     /* op flags a b */
  NS_CMPJ,     /* cond a b label */
  NS_JUMP,     /* label */
  NS_JCOND,    /* cond label */
  NS_FLAGS,    /* op dst cond */
  NS_CALL,     /* tail src descriptor */
  NS_CCALL,    /* helper args descriptor */
  NS_RET,      /* src */
  NS_ADDR,     /* dst label */
  NS_SPBASE,   /* dst offset */
  NS_POLL,     /* label: jump there when the stack pointer is below native.limit */
  NS_FMOV,     /* dst src, doubles */
  NS_FOP2,     /* op dst a b: add, sub, mul, div of doubles */
  NS_FCMPJ,    /* cond a b label, comparing doubles */
  NS_COUNT
};

enum {
  NO_REG,     /* R n */
  NO_IMM,     /* signed value */
  NO_MEM,     /* base offset */
  NO_MEM2,    /* base index shift */
  NO_SLOT,    /* i */
  NO_CELL,    /* unit cell, field */
  NO_CONST,   /* unit literal */
  NO_STATE,   /* state field */
  NO_RT,
  NO_ALLOC,
  NO_CELL_INDEX, /* immediate: the cell number of a unit cell */
  NO_FIXNUM,     /* immediate: the tagged word of a fixnum */
  NO_FREG,       /* float register n */
};

/* State fields the stream can name. */
enum {
  SF_HEAP_END,
  SF_HEAP_START,
  SF_LIMIT,
  SF_ARGS,
  SF_CALL_OTHER,
  SF_NATIVE_SP,
  SF_CALLEE,
  SF_ARGC,
  SF_COUNT
};

static const sljit_s32 mov_ops[] = {SLJIT_MOV,     SLJIT_MOV_U8,  SLJIT_MOV_S8,  SLJIT_MOV_U16,
                                    SLJIT_MOV_S16, SLJIT_MOV_U32, SLJIT_MOV_S32, SLJIT_MOV32};
static const sljit_s32 op2_ops[] = {SLJIT_ADD, SLJIT_SUB,  SLJIT_MUL,  SLJIT_AND,
                                    SLJIT_OR,  SLJIT_XOR,  SLJIT_SHL,  SLJIT_LSHR,
                                    SLJIT_ASHR};

typedef struct {
  uint32_t ra;       /* return address, as an offset from the code start */
  uint32_t frame;    /* the frame's size, as frame_size gives it */
  uint32_t offset;   /* source offset of the call, or UINT32_MAX */
  uint32_t function; /* the oti_function id, or UINT32_MAX for runtime stubs */
  uint32_t slots;    /* first word of the live slot bitmap in the code object */
  uint16_t words;    /* bitmap words */
  /* A call in tail position that keeps its frame, a builtin's: backtraces
   * leave it out when it is innermost, as the VM's do. */
  bool tail;
} oti_descriptor;

typedef struct oti_code {
  uintptr_t start, end;
  oti_descriptor* descriptors;
  size_t descriptor_count;
  uint64_t* bitmaps;
  /* Constant slots the code reads, and the functions it defines. */
  uint32_t *constants, *functions;
  size_t constant_count, function_count;
  bool marked;
} oti_code;

typedef struct oti_code_chunk {
  unsigned char* base;
  size_t used, size;
  struct oti_code_chunk* next;
} oti_code_chunk;

/* Runtime stubs, in a code object of their own. */
enum { STUB_CALL_OTHER, STUB_UNBOUND, STUB_EVALUATOR, STUB_UNWIND, STUB_COUNT };

/* Entries in oti_native_runtime.frames, a power of two. */
#define FRAME_CACHE 64

struct oti_native_runtime {
  oti_code_chunk* chunks;
  oti_code** codes;
  size_t code_count, code_capacity;
  uintptr_t stubs[STUB_COUNT];
  /* In call_other, where a parked builtin call is dispatched again. */
  uintptr_t redispatch;
  /* Segments of the usual size no thread is using, and the chunks they are
   * carved from. */
  oti_segment* free_segments;
  struct oti_segment_chunk* segment_chunks;
  /* Frame sizes by the return address of their entry check's call, so
   * moving a frame to a new segment (oti_poll) needn't look up its
   * descriptor. Cleared when code is freed. */
  struct {
    uintptr_t ra;
    size_t frame;
  } frames[FRAME_CACHE];
  FILE* perf_map;
  bool verbose;
};

/* The trampolines, in the machine's file. */
ot_value oti_native_enter(ot_state* rt, ot_value callee, uintptr_t entry, size_t argc);
extern char oti_native_return[], oti_native_fail[], oti_segment_return[], oti_segment_grow[],
    oti_poll[];

/* --- Segments ------------------------------------------------------------ *
 *
 * A thread's Otium stack is a chain of segments, blocks with this header
 * at the top, so a thread that stays shallow touches only the top page.
 * Frames grow down from under the header, and the limit sits
 * SEGMENT_MARGIN above the bottom: room for a frame that has just been
 * made (frames up to SMALL_FRAME are checked after their prologue), entry
 * records and stub frames. A function that finds itself past the limit
 * moves to a new segment: its frame is copied to the top of the new one,
 * under a link record (the caller's stack pointer and the real return
 * address), and its return address becomes oti_segment_return, which goes
 * back to the old segment. Frame walking follows the link as it follows an
 * entry record. Nothing can point into a frame yet at its entry check, so
 * moving it is safe. Bigger frames check before their prologue and move
 * before they are made (oti_segment_grow).
 *
 * Each segment is twice the size of the one under it, up to
 * SEGMENT_LARGEST, so a deep recursion meets few limits. A recursion that
 * goes back and forth across one crosses it on every call, which upsets
 * branch prediction in the code around it however cheap the crossing is.
 * Segments left on return stay linked above (next) for the next crossing,
 * which then is a few instructions of assembly with no C (oti_poll,
 * oti_segment_return). A thread gives them up when it is switched out,
 * and the running thread at a reclaim (oti_native_stack_trim). */

struct oti_segment {
  oti_segment *prev, *next;
  size_t size;
  size_t unused; /* keeps the top 16-byte aligned */
};

/* Page-aligned, so a thread that stays shallow touches one page. */
#define SEGMENT_BYTES ((size_t)8 << 10)
#define SEGMENT_LARGEST ((size_t)1 << 20)
#define SMALL_FRAME ((size_t)1 << 10)
#define SEGMENT_MARGIN (SMALL_FRAME + 256)
/* The most a thread's segments may add up to. */
#define STACK_MAX ((size_t)256 << 20)
/* Segments of the first size come SEGMENT_CHUNK at a time from one
 * mapping, and go back to the free list, not the system. */
#define SEGMENT_CHUNK 64

typedef struct oti_segment_chunk {
  struct oti_segment_chunk* next;
  void* base;
} oti_segment_chunk;

/* The machine's assembly reads these state fields; the C compiler gives
 * the assembler their offsets. */
void oti_native_offsets(void);
void oti_native_offsets(void) {
  __asm__(".set OFF_SP, %c0\n"
          ".set OFF_ENTRY, %c1\n"
          ".set OFF_TOP, %c2\n"
          ".set OFF_ARGS, %c3\n"
          ".set OFF_DEPTH, %c4\n"
          ".set OFF_HEAP_START, %c5\n"
          ".set OFF_HEAP_USED, %c6\n"
          ".set OFF_CSP, %c7\n"
          ".set OFF_CBASE, %c8\n"
          ".set OFF_CALLEE, %c9\n"
          ".set OFF_ARGC, %c10\n"
          ".set OFF_SEGMENT, %c11\n"
          ".set OFF_STACK_BYTES, %c12\n"
          ".set OFF_BASE_LIMIT, %c13\n"
          ".set OFF_RUNTIME, %c14\n"
          ".set OFF_FRAMES, %c15\n"
          ".set FRAME_MASK, %c16\n"
          ".set SEGMENT_PREV, %c17\n"
          ".set SEGMENT_NEXT, %c18\n"
          ".set SEGMENT_SIZE, %c19\n"
          ".set SEGMENT_LIMIT, %c20\n"
          ".set SEGMENTS_MAX, %c21\n"
          ".set OFF_LIMIT, %c22\n"
          ".set SEGMENTS_MAX_HIGH, %c23\n"
          :
          : "i"(offsetof(ot_state, native.sp)), "i"(offsetof(ot_state, native.entry)),
            "i"(offsetof(ot_state, native.stack_top)), "i"(offsetof(ot_state, native.args)),
            "i"(offsetof(ot_state, native.depth)), "i"(offsetof(ot_state, heap.start)),
            "i"(offsetof(ot_state, heap.used)), "i"(offsetof(ot_state, native_c_sp)),
            "i"(offsetof(ot_state, native_c_base)), "i"(offsetof(ot_state, native.callee)),
            "i"(offsetof(ot_state, native.argc)), "i"(offsetof(ot_state, native.segment)),
            "i"(offsetof(ot_state, native.stack_bytes)), "i"(offsetof(ot_state, native.base_limit)),
            "i"(offsetof(ot_state, native_runtime)), "i"(offsetof(oti_native_runtime, frames)),
            "i"(FRAME_CACHE - 1), "i"(offsetof(oti_segment, prev)), "i"(offsetof(oti_segment, next)),
            "i"(offsetof(oti_segment, size)), "i"(sizeof(oti_segment) + SEGMENT_MARGIN),
            "i"(STACK_MAX), "i"(offsetof(ot_state, native.limit)), "i"(STACK_MAX >> 16));
}


/* Assembler directives, for ELF and Mach-O. A function's inner entry points
 * (ASM_INNER) stay in its block, which Mach-O's linker could otherwise
 * split at any global symbol. */
#ifdef __APPLE__
#define ASM_SYM(name) "_" #name
#define ASM_FUNC(name) ".globl _" #name "\n.private_extern _" #name "\n_" #name ":\n"
#define ASM_INNER(name) ".globl _" #name "\n.private_extern _" #name "\n.alt_entry _" #name "\n_" #name ":\n"
#define ASM_END(name) ""
#define ASM_TEXT ".text\n"
#define ASM_TEXT_END ""
#else
#define ASM_SYM(name) #name
#define ASM_FUNC(name) ".globl " #name "\n.hidden " #name "\n.type " #name ",@function\n" #name ":\n"
#define ASM_INNER(name) ".globl " #name "\n.hidden " #name "\n" #name ":\n"
#define ASM_END(name) ".size " #name ", .-" #name "\n"
#define ASM_TEXT ".pushsection .text\n"
#define ASM_TEXT_END ".popsection\n"
#endif

#if defined(__x86_64__)
#include "native-x86-64.inc"
#elif defined(__aarch64__)
#include "native-arm64.inc"
#else
#error "native code needs x86-64 or arm64"
#endif

static uintptr_t segment_top(const oti_segment* s) { return (uintptr_t)s; }

static uintptr_t segment_base(const oti_segment* s) { return (uintptr_t)s + sizeof(*s) - s->size; }

static uintptr_t segment_limit(const oti_segment* s) { return segment_base(s) + SEGMENT_MARGIN; }

static oti_segment* segment_in(void* block, size_t size) {
  return (oti_segment*)((unsigned char*)block + size - sizeof(oti_segment));
}

/* Fill the free list with a fresh chunk of segments. Untouched pages cost
 * nothing until a thread uses them. */
static bool segment_chunk(oti_native_runtime* n) {
  size_t bytes = SEGMENT_CHUNK * SEGMENT_BYTES;
  oti_segment_chunk* chunk = malloc(sizeof(*chunk));
  void* base = chunk ? mmap(NULL, bytes, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0)
                     : MAP_FAILED;
  if (base == MAP_FAILED) {
    free(chunk);
    return false;
  }
  *chunk = (oti_segment_chunk){n->segment_chunks, base};
  n->segment_chunks = chunk;
  for (size_t i = SEGMENT_CHUNK; i-- > 0;) {
    oti_segment* s = segment_in((unsigned char*)base + i * SEGMENT_BYTES, SEGMENT_BYTES);
    s->prev = n->free_segments;
    n->free_segments = s;
  }
  return true;
}

/* A segment of size bytes, a multiple of the page size. */
static oti_segment* segment_alloc(ot_state* rt, size_t size) {
  oti_native_runtime* n = rt->native_runtime;
  oti_segment* s;
  if (size == SEGMENT_BYTES) {
    if (!n->free_segments && !segment_chunk(n)) return NULL;
    s = n->free_segments;
    n->free_segments = s->prev;
  } else {
    void* block = aligned_alloc(4096, size);
    if (!block) return NULL;
    s = segment_in(block, size);
  }
  *s = (oti_segment){.size = size};
  return s;
}

static void segment_release(ot_state* rt, oti_segment* s) {
  oti_native_runtime* n = rt->native_runtime;
  if (s->size == SEGMENT_BYTES) {
    s->prev = n->free_segments;
    n->free_segments = s;
  } else {
    free((void*)segment_base(s));
  }
}

/* Release the segments kept above s. */
static void release_above(ot_state* rt, oti_segment* s) {
  for (oti_segment* above = s->next; above;) {
    oti_segment* next = above->next;
    segment_release(rt, above);
    above = next;
  }
  s->next = NULL;
}

/* Make s the segment in use, keeping the limit tripped if it is: a signal
 * handler can trip it at any point, so it moves only if it still holds
 * the old base limit. */
static void use_segment(ot_state* rt, oti_segment* s) {
  uintptr_t old = rt->native.base_limit;
  rt->native.segment = s;
  rt->native.base_limit = segment_limit(s);
  __atomic_compare_exchange_n(&rt->native.limit, &old, rt->native.base_limit, false,
                              __ATOMIC_RELAXED, __ATOMIC_RELAXED);
}

/* Leave the segment in use for the one under it, which keeps it. */
static void drop_segment(ot_state* rt) {
  oti_segment* s = rt->native.segment;
  rt->native.stack_bytes -= s->size;
  use_segment(rt, s->prev);
}

bool oti_native_stack_new(ot_state* rt, oti_native_exec* x) {
  oti_segment* s = segment_alloc(rt, SEGMENT_BYTES);
  if (!s) return oti_error(rt, "out of host memory");
  x->segment = s;
  x->stack_bytes = s->size;
  x->stack_top = segment_top(s);
  x->base_limit = x->limit = segment_limit(s);
  return true;
}

void oti_native_stack_free(ot_state* rt, oti_native_exec* x) {
  if (!x->segment) return;
  release_above(rt, x->segment);
  for (oti_segment* s = x->segment; s;) {
    oti_segment* prev = s->prev;
    segment_release(rt, s);
    s = prev;
  }
  x->segment = NULL;
}

void oti_native_stack_trim(ot_state* rt, oti_native_exec* x) {
  if (x->segment) release_above(rt, x->segment);
}

/* The segment above the one in use, with room for size bytes of frame and
 * its link record, or NULL with the error recorded. */
static oti_segment* grow_segment(ot_state* rt, size_t size) {
  oti_segment* below = rt->native.segment;
  size_t need = (size + SEGMENT_MARGIN + sizeof(oti_segment) + 64 + 4095) & ~(size_t)4095;
  size_t bytes = below->size * 2 < SEGMENT_LARGEST ? below->size * 2 : SEGMENT_LARGEST;
  if (need > bytes) bytes = need;
  oti_segment* s = below->next;
  if (s && s->size < need) {
    release_above(rt, below);
    s = NULL;
  }
  if (rt->native.stack_bytes + (s ? s->size : bytes) > STACK_MAX) {
    oti_error(rt, "VM stack limit exceeded");
    return NULL;
  }
  if (!s) {
    if (!(s = segment_alloc(rt, bytes))) {
      oti_error(rt, "out of host memory");
      return NULL;
    }
    s->prev = below;
    below->next = s;
  }
  rt->native.stack_bytes += s->size;
  use_segment(rt, s);
  return s;
}

/* Put a link record at the top of s: the stack pointer and return address
 * of the frame below it, a moved one's or the one oti_segment_return goes
 * back to. Returns the record, where that frame's stack pointer would be. */
static uintptr_t link_segment(oti_segment* s, uintptr_t caller_sp, uintptr_t return_address) {
  uintptr_t* link = (uintptr_t*)(segment_top(s) - 16);
  link[0] = caller_sp;
  link[1] = return_address;
  return (uintptr_t)link;
}

static const oti_descriptor* descriptor_at(const oti_native_runtime* n, uintptr_t ra,
                                           const oti_code** owner);

/* The function whose entry check called h_poll made its frame past the
 * limit: move the frame, with the return address of the call into C under
 * it, to a new segment. */
static bool grow_frame(ot_state* rt) {
  uintptr_t sp = rt->native.sp, ra = ((uintptr_t*)sp)[-1];
  oti_native_runtime* n = rt->native_runtime;
  size_t cached = (ra >> 3) & (FRAME_CACHE - 1);
  if (n->frames[cached].ra != ra) {
    const oti_descriptor* d = descriptor_at(n, ra, NULL);
    if (!d) abort();
    n->frames[cached].ra = ra;
    n->frames[cached].frame = d->frame;
  }
  size_t frame = n->frames[cached].frame, span = frame_span(frame);
  oti_segment* s = grow_segment(rt, span + 16);
  if (!s) return false;
  uintptr_t moved = link_segment(s, sp + span, *return_slot(sp, frame)) - span;
  memcpy((void*)moved, (void*)sp, frame);
  *return_slot(moved, frame) = (uintptr_t)oti_segment_return;
  ((uintptr_t*)moved)[-1] = ra;
  rt->native.sp = moved;
  return true;
}

/* oti_segment_grow: a big frame's check before its prologue failed. sp
 * points at the 80 bytes the trampoline saved, the argument registers and
 * where to continue, with the function's own return address in the last
 * word, at the caller's sp[-1]; size is what the frame needs. Returns the
 * new segment's link record, under which the trampoline continues the
 * function. */
uintptr_t oti_segment_grow_entry(ot_state* rt, size_t size, uintptr_t sp);
uintptr_t oti_segment_grow_entry(ot_state* rt, size_t size, uintptr_t sp) {
  oti_segment* s = grow_segment(rt, size);
  if (!s) {
    /* Raise from the caller's call. */
    rt->native.sp = sp + 80;
    oti_native_raise(rt);
  }
  return link_segment(s, sp + 80, ((uintptr_t*)sp)[9]);
}

/* Unwinding to sp: drop the segments above the one that holds it. */
static void unwind_segments(ot_state* rt, uintptr_t sp) {
  while (!(sp > segment_base(rt->native.segment) && sp <= segment_top(rt->native.segment)))
    drop_segment(rt);
}


/* --- Code space -------------------------------------------------------- */

#define CODE_CHUNK ((size_t)4 << 20)

static size_t page_size(void) {
  static size_t page;
  if (!page) page = (size_t)sysconf(_SC_PAGESIZE);
  return page;
}

/* Pages are writable only while sljit fills them, then executable. */
static void protect(void* start, size_t size, int prot) {
  uintptr_t page = page_size(), from = (uintptr_t)start & ~(page - 1);
  uintptr_t to = ((uintptr_t)start + size + page - 1) & ~(page - 1);
  if (mprotect((void*)from, to - from, prot)) {
    perror("otium: code space protection");
    abort();
  }
}

void* oti_code_space_alloc(void* space, size_t size) {
  oti_native_runtime* n = space;
  size = (size + 15) & ~(size_t)15;
  oti_code_chunk* c = n->chunks;
  if (!c || c->size - c->used < size) {
    size_t bytes = size > CODE_CHUNK ? (size + page_size() - 1) & ~(page_size() - 1) : CODE_CHUNK;
    void* base = mmap(NULL, bytes, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if (base == MAP_FAILED) return NULL;
    c = malloc(sizeof(*c));
    if (!c) {
      munmap(base, bytes);
      return NULL;
    }
    *c = (oti_code_chunk){base, 0, bytes, n->chunks};
    n->chunks = c;
  }
  void* p = c->base + c->used;
  c->used += size;
  protect(p, size, PROT_READ | PROT_WRITE);
  return p;
}

/* Code memory is kept for now; reclaimed code objects only free their
 * metadata. */
void oti_code_space_free(void* space, void* code) {
  (void)space;
  (void)code;
}

/* --- Code objects ------------------------------------------------------ */

static oti_code* code_at(const oti_native_runtime* n, uintptr_t address) {
  size_t low = 0, high = n->code_count;
  while (low < high) {
    size_t middle = low + (high - low) / 2;
    oti_code* c = n->codes[middle];
    if (address < c->start) high = middle;
    else if (address >= c->end) low = middle + 1;
    else return c;
  }
  return NULL;
}

static const oti_descriptor* descriptor_at(const oti_native_runtime* n, uintptr_t ra,
                                           const oti_code** owner) {
  const oti_code* c = code_at(n, ra);
  if (!c) return NULL;
  uint32_t offset = (uint32_t)(ra - c->start);
  size_t low = 0, high = c->descriptor_count;
  while (low < high) {
    size_t middle = low + (high - low) / 2;
    if (c->descriptors[middle].ra < offset) low = middle + 1;
    else high = middle;
  }
  if (low == c->descriptor_count || c->descriptors[low].ra != offset) return NULL;
  if (owner) *owner = c;
  return &c->descriptors[low];
}

static bool add_code(ot_state* rt, oti_code* code) {
  oti_native_runtime* n = rt->native_runtime;
  if (!oti_grow(rt, (void**)&n->codes, &n->code_capacity, n->code_count + 1, sizeof(oti_code*)))
    return false;
  size_t i = n->code_count;
  while (i && n->codes[i - 1]->start > code->start) {
    n->codes[i] = n->codes[i - 1];
    i--;
  }
  n->codes[i] = code;
  n->code_count++;
  return true;
}

static void free_code(oti_code* code) {
  free(code->descriptors);
  free(code->bitmaps);
  free(code->constants);
  free(code->functions);
  free(code);
}

/* --- The bridge -------------------------------------------------------- */

typedef struct {
  struct sljit_jump* jump;
  uint32_t label;
} oti_pending_jump;

typedef struct {
  struct sljit_label* label;
  uint32_t descriptor, frame, function;
} oti_site;

typedef struct {
  ot_state* rt;
  struct sljit_compiler* c;
  const unsigned char *p, *end;
  bool bad;
  const uint32_t *cells, *constants;
  size_t cell_count, constant_count;
  struct sljit_label** labels;
  size_t label_count;
  oti_pending_jump* jumps;
  size_t jump_count, jump_capacity;
  oti_site* sites;
  size_t site_count, site_capacity;
  struct sljit_label** entries;
  size_t function_count;
  uint32_t function, frame;
  const uint32_t* function_ids;
} oti_bridge;

static uint64_t read_unsigned(oti_bridge* b) {
  uint64_t value = 0;
  for (unsigned shift = 0; shift < 64; shift += 7) {
    if (b->p >= b->end) {
      b->bad = true;
      return 0;
    }
    unsigned char byte = *b->p++;
    value |= (uint64_t)(byte & 127) << shift;
    if (!(byte & 128)) return value;
  }
  b->bad = true;
  return 0;
}

/* Signed LEB128, which the Otium side writes without overflowing fixnums. */
static int64_t read_signed(oti_bridge* b) {
  uint64_t value = 0;
  for (unsigned shift = 0; shift < 70; shift += 7) {
    if (b->p >= b->end) {
      b->bad = true;
      return 0;
    }
    unsigned char byte = *b->p++;
    if (shift < 64) value |= (uint64_t)(byte & 127) << shift;
    if (!(byte & 128)) {
      if (shift + 7 < 64 && (byte & 64)) value |= ~(uint64_t)0 << (shift + 7);
      return (int64_t)value;
    }
  }
  b->bad = true;
  return 0;
}

static sljit_s32 register_operand(oti_bridge* b) {
  uint64_t r = read_unsigned(b);
  if (r <= 10) return SLJIT_R((sljit_s32)r);
  if (r == 11) return SLJIT_S0;
  if (r == 12) return SLJIT_S1;
  b->bad = true;
  return SLJIT_R0;
}

static sljit_sw state_offset(uint64_t field, bool* bad) {
  switch (field) {
    case SF_HEAP_END: return (sljit_sw)offsetof(ot_state, heap.end);
    case SF_HEAP_START: return (sljit_sw)offsetof(ot_state, heap.start);
    case SF_LIMIT: return (sljit_sw)offsetof(ot_state, native.limit);
    case SF_ARGS: return (sljit_sw)offsetof(ot_state, native.args);
    case SF_CALL_OTHER: return (sljit_sw)offsetof(ot_state, native_stubs[STUB_CALL_OTHER]);
    case SF_NATIVE_SP: return (sljit_sw)offsetof(ot_state, native.sp);
    case SF_CALLEE: return (sljit_sw)offsetof(ot_state, native.callee);
    case SF_ARGC: return (sljit_sw)offsetof(ot_state, native.argc);
    default: *bad = true; return 0;
  }
}

static void operand(oti_bridge* b, sljit_s32* op, sljit_sw* w) {
  uint64_t kind = read_unsigned(b), i;
  *w = 0;
  switch (kind) {
    case NO_REG: *op = register_operand(b); return;
    case NO_IMM:
      *op = SLJIT_IMM;
      *w = (sljit_sw)read_signed(b);
      return;
    case NO_MEM:
      *op = SLJIT_MEM1(register_operand(b));
      *w = (sljit_sw)read_signed(b);
      return;
    case NO_MEM2: {
      sljit_s32 base = register_operand(b), index = register_operand(b);
      *op = SLJIT_MEM2(base, index);
      *w = (sljit_sw)read_unsigned(b);
      if (*w > 3) b->bad = true;
      return;
    }
    case NO_SLOT:
      *op = SLJIT_MEM1(SLJIT_SP);
      *w = (sljit_sw)read_unsigned(b) * 8;
      return;
    case NO_CELL: {
      i = read_unsigned(b);
      uint64_t field = read_unsigned(b);
      if (i >= b->cell_count || field > 1) {
        b->bad = true;
        i = 0;
      }
      *op = SLJIT_MEM1(SLJIT_S0);
      *w = (sljit_sw)(OTI_CELLS_OFFSET + (b->cell_count ? b->cells[i] : 0) * sizeof(oti_cell) +
                      field * sizeof(ot_value));
      return;
    }
    case NO_CONST:
      i = read_unsigned(b);
      if (i >= b->constant_count) {
        b->bad = true;
        i = 0;
      }
      *op = SLJIT_MEM1(SLJIT_S0);
      *w = (sljit_sw)(OTI_CONSTANTS_OFFSET +
                      (b->constant_count ? b->constants[i] : 0) * sizeof(ot_value));
      return;
    case NO_STATE:
      *op = SLJIT_MEM1(SLJIT_S0);
      *w = state_offset(read_unsigned(b), &b->bad);
      return;
    case NO_RT: *op = SLJIT_S0; return;
    case NO_ALLOC: *op = SLJIT_S1; return;
    case NO_CELL_INDEX:
      i = read_unsigned(b);
      if (i >= b->cell_count) {
        b->bad = true;
        i = 0;
      }
      *op = SLJIT_IMM;
      *w = b->cell_count ? (sljit_sw)b->cells[i] : 0;
      return;
    case NO_FREG: {
      uint64_t r = read_unsigned(b);
      if (r > 1) b->bad = true;
      *op = SLJIT_FR((sljit_s32)(r & 1));
      return;
    }
    case NO_FIXNUM:
      *op = SLJIT_IMM;
      *w = (sljit_sw)(((uint64_t)read_signed(b) << 1) | 1);
      return;
    default: b->bad = true; *op = SLJIT_IMM;
  }
}

static uint32_t read_label(oti_bridge* b) {
  uint64_t l = read_unsigned(b);
  if (l >= b->label_count) {
    b->bad = true;
    return 0;
  }
  return (uint32_t)l;
}

static void pend(oti_bridge* b, struct sljit_jump* jump, uint32_t label) {
  if (!jump) {
    b->bad = true;
    return;
  }
  if (!oti_grow(b->rt, (void**)&b->jumps, &b->jump_capacity, b->jump_count + 1,
                sizeof(oti_pending_jump))) {
    b->bad = true;
    return;
  }
  b->jumps[b->jump_count++] = (oti_pending_jump){jump, label};
}

static void site(oti_bridge* b, uint64_t descriptor) {
  if (!oti_grow(b->rt, (void**)&b->sites, &b->site_capacity, b->site_count + 1,
                sizeof(oti_site))) {
    b->bad = true;
    return;
  }
  b->sites[b->site_count++] =
      (oti_site){sljit_emit_label(b->c), (uint32_t)descriptor, b->frame, b->function};
}

static const void* helper_address(uint64_t id);

/* A call into C, through the function's bridge (BRIDGE), which runs it on
 * the C stack: S1 goes back to heap.used first, so the collector sees it,
 * and is reloaded after. The state goes in R0, ahead of the arguments the
 * stream put in R1 up. */
static void c_call(struct sljit_compiler* c, const void* bridge, uint64_t args) {
  static const sljit_s32 types[] = {
      SLJIT_ARGS1(W, P), SLJIT_ARGS2(W, P, W), SLJIT_ARGS3(W, P, W, W), SLJIT_ARGS4(W, P, W, W, W)};
  sljit_emit_op2(c, SLJIT_SUB, SLJIT_MEM1(SLJIT_S0), (sljit_sw)offsetof(ot_state, heap.used),
                 SLJIT_S1, 0, SLJIT_MEM1(SLJIT_S0), (sljit_sw)offsetof(ot_state, heap.start));
  sljit_emit_op1(c, SLJIT_MOV, SLJIT_R0, 0, SLJIT_S0, 0);
  sljit_emit_icall(c, SLJIT_CALL, types[args], SLJIT_IMM, SLJIT_FUNC_ADDR(bridge));
}

static void after_c_call(struct sljit_compiler* c) {
  sljit_emit_op2(c, SLJIT_ADD, SLJIT_S1, 0, SLJIT_MEM1(SLJIT_S0),
                 (sljit_sw)offsetof(ot_state, heap.start), SLJIT_MEM1(SLJIT_S0),
                 (sljit_sw)offsetof(ot_state, heap.used));
}

/* Two scratch float registers, for float fast paths; they never hold values
 * across a call. */
static void enter(struct sljit_compiler* c, sljit_s32 scratches, sljit_s32 slots, uint32_t* frame) {
  size_t need = (size_t)slots * 8 + 256;
  if (need > SMALL_FRAME) check_big_frame(c, need);
  sljit_emit_enter(c, SLJIT_ENTER_REG_ARG | SLJIT_ENTER_KEEP(2), SLJIT_ARGS0(W),
                   scratches | SLJIT_ENTER_FLOAT(2), 2, slots * 8);
  *frame = frame_size(c, scratches);
}

static bool translate(oti_bridge* b) {
  struct sljit_compiler* c = b->c;
  sljit_s32 d, s1, s2;
  sljit_sw dw, s1w, s2w;
  for (;;) {
    if (b->bad) return false;
    uint64_t op = read_unsigned(b);
    switch (op) {
      case NS_END: return !b->bad;
      case NS_FUNC: {
        uint64_t f = read_unsigned(b);
        if (f >= b->function_count) return false;
        b->function = b->function_ids[f];
        b->entries[f] = sljit_emit_aligned_label(c, SLJIT_LABEL_ALIGN_16, NULL);
        break;
      }
      case NS_ENTER: {
        uint64_t scratches = read_unsigned(b), slots = read_unsigned(b);
        if (scratches < 7 || scratches > 11 || slots > 100000) return false;
        enter(c, (sljit_s32)scratches, (sljit_s32)slots, &b->frame);
        break;
      }
      case NS_LABEL: {
        uint32_t l = read_label(b);
        if (b->labels[l]) return false;
        b->labels[l] = sljit_emit_label(c);
        break;
      }
      case NS_MOV: {
        uint64_t mode = read_unsigned(b);
        if (mode >= sizeof(mov_ops) / sizeof(mov_ops[0])) return false;
        operand(b, &d, &dw);
        operand(b, &s1, &s1w);
        sljit_emit_op1(c, mov_ops[mode], d, dw, s1, s1w);
        break;
      }
      case NS_OP2:
      case NS_OP2U: {
        uint64_t code = read_unsigned(b), flags = read_unsigned(b);
        if (code >= sizeof(op2_ops) / sizeof(op2_ops[0])) return false;
        sljit_s32 o = op2_ops[code] | (flags == 1 ? SLJIT_SET_Z : flags ? SLJIT_SET((sljit_s32)(flags - 2)) : 0);
        if (op == NS_OP2) {
          operand(b, &d, &dw);
          operand(b, &s1, &s1w);
          operand(b, &s2, &s2w);
          sljit_emit_op2(c, o, d, dw, s1, s1w, s2, s2w);
        } else {
          operand(b, &s1, &s1w);
          operand(b, &s2, &s2w);
          sljit_emit_op2u(c, o, s1, s1w, s2, s2w);
        }
        break;
      }
      case NS_CMPJ: {
        uint64_t cond = read_unsigned(b);
        if (cond > SLJIT_SIG_LESS_EQUAL) return false;
        operand(b, &s1, &s1w);
        operand(b, &s2, &s2w);
        uint32_t l = read_label(b);
        pend(b, sljit_emit_cmp(c, (sljit_s32)cond, s1, s1w, s2, s2w), l);
        break;
      }
      case NS_JUMP: pend(b, sljit_emit_jump(c, SLJIT_JUMP), read_label(b)); break;
      case NS_JCOND: {
        uint64_t cond = read_unsigned(b);
        if (cond > SLJIT_NOT_CARRY) return false;
        pend(b, sljit_emit_jump(c, (sljit_s32)cond), read_label(b));
        break;
      }
      case NS_FLAGS: {
        uint64_t o = read_unsigned(b);
        operand(b, &d, &dw);
        uint64_t cond = read_unsigned(b);
        if (o > 2 || cond > SLJIT_NOT_CARRY) return false;
        static const sljit_s32 flag_ops[] = {SLJIT_MOV, SLJIT_OR, SLJIT_AND};
        sljit_emit_op_flags(c, flag_ops[o], d, dw, (sljit_s32)cond);
        break;
      }
      case NS_CALL: {
        uint64_t tail = read_unsigned(b);
        operand(b, &s1, &s1w);
        uint64_t descriptor = read_unsigned(b);
        sljit_emit_icall(c, SLJIT_CALL_REG_ARG | (tail ? SLJIT_CALL_RETURN : 0), SLJIT_ARGS0(W),
                         s1, s1w);
        if (!tail) site(b, descriptor);
        break;
      }
      case NS_CCALL: {
        uint64_t helper = read_unsigned(b), args = read_unsigned(b);
        uint64_t descriptor = read_unsigned(b);
        const void* address = helper_address(helper);
        if (!address || args > 3) return false;
        c_call(c, address, args);
        site(b, descriptor);
        after_c_call(c);
        break;
      }
      case NS_RET:
        operand(b, &s1, &s1w);
        sljit_emit_return(c, SLJIT_MOV, s1, s1w);
        break;
      case NS_ADDR: {
        operand(b, &d, &dw);
        pend(b, sljit_emit_op_addr(c, SLJIT_MOV_ADDR, d, dw), read_label(b));
        break;
      }
      case NS_SPBASE:
        /* Offsets are from the stack pointer, not from the locals. */
        operand(b, &d, &dw);
        sljit_get_local_base(c, d, dw, (sljit_sw)read_signed(b) - SLJIT_LOCALS_OFFSET);
        break;
      case NS_POLL: pend(b, poll(c), read_label(b)); break;
      case NS_FMOV:
        operand(b, &d, &dw);
        operand(b, &s1, &s1w);
        sljit_emit_fop1(c, SLJIT_MOV_F64, d, dw, s1, s1w);
        break;
      case NS_FOP2: {
        static const sljit_s32 fops[] = {SLJIT_ADD_F64, SLJIT_SUB_F64, SLJIT_MUL_F64, SLJIT_DIV_F64};
        uint64_t code = read_unsigned(b);
        if (code > 3) return false;
        operand(b, &d, &dw);
        operand(b, &s1, &s1w);
        operand(b, &s2, &s2w);
        sljit_emit_fop2(c, fops[code], d, dw, s1, s1w, s2, s2w);
        break;
      }
      case NS_FCMPJ: {
        uint64_t cond = read_unsigned(b);
        if (cond < SLJIT_F_EQUAL || cond > SLJIT_ORDERED_LESS_EQUAL) return false;
        operand(b, &s1, &s1w);
        operand(b, &s2, &s2w);
        uint32_t l = read_label(b);
        pend(b, sljit_emit_fcmp(c, (sljit_s32)cond, s1, s1w, s2, s2w), l);
        break;
      }
      default: return false;
    }
    if (sljit_get_compiler_error(c)) return false;
  }
}

/* --- Function records -------------------------------------------------- */

static uint32_t register_function(ot_state* rt) {
  size_t id = rt->function_free;
  while (id < rt->function_count && rt->functions[id]) id++;
  if (id >= UINT32_MAX || !oti_grow(rt, (void**)&rt->functions, &rt->function_capacity, id + 1,
                                    sizeof(oti_function*))) {
    oti_error(rt, "too many functions");
    return UINT32_MAX;
  }
  oti_function* f = calloc(1, sizeof(*f));
  if (!f) {
    oti_error(rt, "out of host memory");
    return UINT32_MAX;
  }
  f->namespace = rt->current_namespace;
  f->source = rt->source;
  if (id == rt->function_count) rt->function_count++;
  rt->function_free = id + 1;
  rt->functions[id] = f;
  return (uint32_t)id;
}

static void release_function(ot_state* rt, uint32_t id) {
  if (id >= rt->function_count) return;
  oti_free_function(rt->functions[id]);
  rt->functions[id] = NULL;
  if (id < rt->function_free) rt->function_free = id;
}

/* A constant slot nothing else shares, for a unit's static closures. */
static uint32_t fresh_constant(ot_state* rt, ot_value value) {
  size_t index = rt->constant_free;
  while (index < rt->constant_count && rt->constant_state[index]) index++;
  if (index >= OTI_CONSTANT_RESERVE ||
      !oti_grow(rt, (void**)&rt->constant_state, &rt->constant_state_capacity, index + 1, 1)) {
    oti_error(rt, "too many constants");
    return UINT32_MAX;
  }
  if (index == rt->constant_count) rt->constant_count++;
  rt->constant_state[index] = 1;
  rt->constants[index] = value;
  rt->constant_free = index + 1;
  rt->constant_roots.count = rt->constant_count;
  return (uint32_t)index;
}

oti_function* oti_closure_fn(ot_state* rt, ot_value closure) {
  if (!rt->native_mode) return rt->functions[oti_closure_function(closure)];
  uintptr_t entry = oti_as_closure(closure)->code;
  const oti_code* c = code_at(rt->native_runtime, entry);
  if (!c) return NULL;
  for (size_t i = 0; i < c->function_count; i++) {
    oti_function* f = rt->functions[c->functions[i]];
    if (f && f->entry == entry) return f;
  }
  return NULL;
}

/* --- Installing units -------------------------------------------------- */

static ot_value* array_items(ot_value array, size_t* count) {
  if (!oti_kind(array, K_ARRAY)) {
    *count = 0;
    return NULL;
  }
  *count = oti_as_array(array)->length;
  if (!*count) return NULL;
  return oti_as_array_data(oti_as_array(array)->data)->items;
}

static bool unit_int(ot_value v, uint32_t* out) {
  if (!ot_is_int(v) || ot_to_int(v) < 0 || ot_to_int(v) > (intptr_t)UINT32_MAX - 1) return false;
  *out = (uint32_t)ot_to_int(v);
  return true;
}

static int compare_descriptors(const void* a, const void* b) {
  const oti_descriptor *x = a, *y = b;
  return x->ra < y->ra ? -1 : x->ra > y->ra;
}

static void write_perf_map(ot_state* rt, oti_code* code) {
  oti_native_runtime* n = rt->native_runtime;
  if (!n->perf_map) return;
  for (size_t i = 0; i < code->function_count; i++) {
    oti_function* f = rt->functions[code->functions[i]];
    uintptr_t end = i + 1 < code->function_count ? rt->functions[code->functions[i + 1]]->entry
                                                 : code->end;
    fprintf(n->perf_map, "%lx %lx %s\n", (unsigned long)f->entry, (unsigned long)(end - f->entry),
            f->name ? f->name : "fn");
  }
  fflush(n->perf_map);
}

/* Install a unit: [stream literals cells functions descriptors statics
 * max-args].
 * functions are [name arity optional rest? offset]; descriptors are
 * [function live-slots offset]; statics are [function literal], closures
 * without captures that the unit reads as literals. Returns the static
 * closure of function 0, the unit's top level. */
ot_value oti_native_install(ot_state* rt, size_t slot) {
  if (getenv("OTIUM_DUMP_UNITS")) {
    ot_write(rt, stderr, rt->stack[slot]);
    fputc('\n', stderr);
  }
  size_t parts_count;
  ot_value* parts = array_items(rt->stack[slot], &parts_count);
  if (parts_count < 6 || !oti_kind(parts[0], K_BYTEVECTOR)) {
    oti_error(rt, "native unit: malformed");
    return 0;
  }
  size_t literal_count, cell_count, function_count, descriptor_count, static_count;
  array_items(parts[1], &literal_count);
  array_items(parts[2], &cell_count);
  array_items(parts[3], &function_count);
  array_items(parts[4], &descriptor_count);
  array_items(parts[5], &static_count);
  if (!function_count) {
    oti_error(rt, "native unit: no functions");
    return 0;
  }
  /* Calls store arguments past the fifth in native.args. */
  uint32_t max_args = 0;
  if (parts_count > 6 && unit_int(parts[6], &max_args) && !oti_native_args(rt, max_args)) return 0;
  oti_bridge b = {.rt = rt};
  oti_code* code = calloc(1, sizeof(*code));
  uint32_t* cells = calloc(cell_count + 1, sizeof(uint32_t));
  uint32_t* constants = calloc(literal_count + 1, sizeof(uint32_t));
  uint32_t* functions = calloc(function_count, sizeof(uint32_t));
  b.entries = calloc(function_count, sizeof(struct sljit_label*));
  bool ok = code && cells && constants && functions && b.entries;
  if (!ok) oti_error(rt, "out of host memory");
  size_t made_functions = 0;
  bool installed = false;
  /* Static closures get fresh slots, filled once their code exists. */
  for (size_t i = 0; ok && i < literal_count; i++) {
    ot_value* literals = array_items(parts[1], &literal_count);
    bool fixed = false;
    ot_value* statics = array_items(parts[5], &static_count);
    for (size_t s = 0; s < static_count; s++) {
      size_t n;
      ot_value* entry = array_items(statics[s], &n);
      uint32_t literal;
      if (n == 2 && unit_int(entry[1], &literal) && literal == i) fixed = true;
    }
    constants[i] = fixed ? fresh_constant(rt, OT_NIL) : oti_constant(rt, literals[i]);
    ok = constants[i] != UINT32_MAX;
  }
  for (size_t i = 0; ok && i < cell_count; i++) {
    ot_value* items = array_items(parts[2], &cell_count);
    ok = unit_int(items[i], &cells[i]) && cells[i] < rt->global_count;
    if (!ok) oti_error(rt, "native unit: bad cell");
  }
  for (size_t i = 0; ok && i < function_count; i++) {
    uint32_t id = register_function(rt);
    if (id == UINT32_MAX) {
      ok = false;
      break;
    }
    functions[made_functions++] = id;
    ot_value* items = array_items(parts[3], &function_count);
    size_t n;
    ot_value* info = array_items(items[i], &n);
    uint32_t arity, optional, offset = UINT32_MAX;
    if (n < 5 || !unit_int(info[1], &arity) || !unit_int(info[2], &optional)) {
      ok = oti_error(rt, "native unit: bad function");
      break;
    }
    oti_function* f = rt->functions[id];
    f->arity = arity;
    f->optional = optional;
    f->rest = oti_truth(info[3]);
    f->variadic = f->rest || f->optional;
    if (unit_int(info[4], &offset)) f->offset = offset;
    else f->offset = UINT32_MAX;
    if (oti_kind(info[0], K_SYMBOL) || oti_kind(info[0], K_STRING)) {
      size_t length;
      const char* text = oti_text_bytes(info[0], &length);
      f->name = malloc(length + 1);
      if (f->name) {
        memcpy(f->name, text, length);
        f->name[length] = 0;
      }
    }
  }
  /* Descriptors, with their bitmaps, in stream order. */
  oti_descriptor* descriptors = calloc(descriptor_count + 1, sizeof(oti_descriptor));
  uint64_t* bitmaps = NULL;
  size_t bitmap_count = 0, bitmap_capacity = 0;
  if (ok && !descriptors) ok = oti_error(rt, "out of host memory");
  for (size_t i = 0; ok && i < descriptor_count; i++) {
    ot_value* items = array_items(parts[4], &descriptor_count);
    size_t n, slot_count;
    ot_value* info = array_items(items[i], &n);
    uint32_t function = UINT32_MAX, offset = UINT32_MAX;
    if (n < 3 || (info[0] != OT_NIL && (!unit_int(info[0], &function) || function >= function_count))) {
      ok = oti_error(rt, "native unit: bad descriptor");
      break;
    }
    unit_int(info[2], &offset);
    ot_value* slots = array_items(info[1], &slot_count);
    uint32_t highest = 0;
    for (size_t s = 0; s < slot_count; s++) {
      uint32_t slot;
      if (!unit_int(slots[s], &slot)) ok = oti_error(rt, "native unit: bad slot");
      else if (slot + 1 > highest) highest = slot + 1;
    }
    uint32_t words = (highest + 63) / 64;
    if (!ok || !oti_grow(rt, (void**)&bitmaps, &bitmap_capacity, bitmap_count + words + 1,
                         sizeof(uint64_t)))
      break;
    memset(bitmaps + bitmap_count, 0, (words + 1) * sizeof(uint64_t));
    for (size_t s = 0; s < slot_count; s++) {
      uint32_t slot = (uint32_t)ot_to_int(slots[s]);
      bitmaps[bitmap_count + slot / 64] |= (uint64_t)1 << (slot % 64);
    }
    descriptors[i] = (oti_descriptor){0, 0, offset,
                                      function == UINT32_MAX ? UINT32_MAX : functions[function],
                                      (uint32_t)bitmap_count, (uint16_t)words,
                                      n > 3 && oti_truth(info[3])};
    bitmap_count += words;
  }
  /* The stream. */
  size_t stream_length = ok ? oti_bytes_length(parts[0]) : 0;
  unsigned char* stream = ok ? malloc(stream_length + 1) : NULL;
  if (ok && !stream) ok = oti_error(rt, "out of host memory");
  if (ok) memcpy(stream, oti_bytes_data(parts[0]), stream_length);
  void* machine = NULL;
  if (ok) {
    b.c = sljit_create_compiler(NULL);
    b.p = stream;
    b.end = stream + stream_length;
    b.cells = cells;
    b.cell_count = cell_count;
    b.constants = constants;
    b.constant_count = literal_count;
    b.function_count = function_count;
    b.function_ids = functions;
    /* Labels are numbered densely by the compiler; count them first. */
    uint64_t labels = read_unsigned(&b);
    b.label_count = (size_t)labels;
    b.labels = calloc(b.label_count + 1, sizeof(struct sljit_label*));
    ok = b.c && b.labels && !b.bad;
    if (ok && rt->native_runtime->verbose && rt->core_loaded) sljit_compiler_verbose(b.c, stderr);
    if (ok) ok = translate(&b);
    for (size_t i = 0; ok && i < b.jump_count; i++) {
      struct sljit_label* target = b.labels[b.jumps[i].label];
      if (!target) ok = false;
      else sljit_set_label(b.jumps[i].jump, target);
    }
    for (size_t i = 0; ok && i < function_count; i++) ok = b.entries[i] != NULL;
    for (size_t i = 0; ok && i < b.site_count; i++) ok = b.sites[i].descriptor < descriptor_count;
    if (ok) {
      machine = sljit_generate_code(b.c, 0, rt->native_runtime);
      ok = machine != NULL;
    }
    if (!ok && !oti_failed(rt))
      oti_errorf(rt, "native unit rejected (sljit error %d)", b.c ? sljit_get_compiler_error(b.c) : -1);
  }
  if (ok) {
    size_t size = sljit_get_generated_code_size(b.c);
    protect(machine, size, PROT_READ | PROT_EXEC);
    code->start = (uintptr_t)machine;
    code->end = code->start + size;
    for (size_t i = 0; i < b.site_count; i++) {
      oti_descriptor* d = &descriptors[b.sites[i].descriptor];
      d->ra = (uint32_t)(sljit_get_label_addr(b.sites[i].label) - code->start);
      d->frame = b.sites[i].frame;
    }
    for (size_t i = 0; i < function_count; i++)
      rt->functions[functions[i]]->entry = sljit_get_label_addr(b.entries[i]);
    qsort(descriptors, descriptor_count, sizeof(oti_descriptor), compare_descriptors);
    code->descriptors = descriptors;
    code->descriptor_count = descriptor_count;
    code->bitmaps = bitmaps;
    code->constants = constants;
    code->constant_count = literal_count;
    code->functions = functions;
    code->function_count = function_count;
    descriptors = NULL;
    bitmaps = NULL;
    ok = add_code(rt, code);
    if (ok) {
      installed = true;
      write_perf_map(rt, code);
    } else {
      code->descriptors = NULL;
      code->bitmaps = NULL;
    }
  }
  ot_value result = 0;
  uint32_t top = UINT32_MAX;
  for (size_t i = 0; ok && i < static_count; i++) {
    ot_value closure = oti_alloc_closure(rt, 0);
    if (!closure) {
      ok = false;
      break;
    }
    /* Allocation may have moved the unit. */
    parts = array_items(rt->stack[slot], &parts_count);
    size_t n;
    ot_value* entry = array_items(array_items(parts[5], &static_count)[i], &n);
    uint32_t function, literal;
    if (n != 2 || !unit_int(entry[0], &function) || !unit_int(entry[1], &literal) ||
        function >= function_count || literal >= literal_count) {
      ok = oti_error(rt, "native unit: bad static closure");
      break;
    }
    oti_as_closure(closure)->code = rt->functions[code->functions[function]]->entry;
    rt->constants[code->constants[literal]] = closure;
    if (function == 0) top = code->constants[literal];
  }
  /* Later allocations may have moved it; the constant slot is a root. */
  if (ok && top != UINT32_MAX) result = rt->constants[top];
  if (ok && !result) ok = oti_error(rt, "native unit: no static top level");
  if (b.c) sljit_free_compiler(b.c);
  free(b.labels);
  free(b.jumps);
  free(b.sites);
  free(b.entries);
  free(stream);
  free(cells);
  free(descriptors);
  free(bitmaps);
  if (installed) return ok ? result : 0;
  /* Nothing refers to a rejected unit's functions yet. Its constants are
   * left for oti_reclaim. */
  for (size_t i = 0; i < made_functions; i++) release_function(rt, functions[i]);
  free(functions);
  free(constants);
  free(code);
  return 0;
}

/* --- Calling native code from C ---------------------------------------- */

bool oti_native_args(ot_state* rt, size_t count) {
  if (count + 8 <= rt->native.args_capacity) return true;
  size_t old = rt->native.args_capacity;
  if (!oti_grow(rt, (void**)&rt->native.args, &rt->native.args_capacity, count + 8,
                sizeof(ot_value)))
    return false;
  for (size_t i = old; i < rt->native.args_capacity; i++) rt->native.args[i] = OT_NIL;
  /* Compiled code assumes room for any unit's arguments whichever thread
   * installed it, so every thread grows together. */
  return oti_threads_grow_args(rt);
}

static uintptr_t entry_of(ot_state* rt, ot_value callee) {
  return oti_kind(callee, K_CLOSURE) ? oti_as_closure(callee)->code
                                     : rt->native_stubs[STUB_CALL_OTHER];
}

uintptr_t oti_native_entry_of(ot_state* rt, ot_value callee) { return entry_of(rt, callee); }

uintptr_t oti_native_redispatch(ot_state* rt) { return rt->native_runtime->redispatch; }

/* Call callee with argc arguments already in native.args. 0 on failure, with
 * the error recorded, as oti_call_list. */
ot_value oti_native_invoke(ot_state* rt, ot_value callee, size_t argc) {
  if (!oti_host_entry(rt)) return 0;
  size_t base = rt->sp, tries = rt->try_count, params = rt->param_count;
  size_t handlers = rt->handler_count, handler_top = rt->handler_top,
         restarts = rt->restart_scope_count, cleanups = rt->cleanup_count;
  rt->frame_count++;
  ot_value result = oti_native_enter(rt, callee, entry_of(rt, callee), argc);
  rt->frame_count--;
  rt->try_count = tries;
  oti_restore_restarts(rt, restarts);
  oti_restore_cleanups(rt, cleanups);
  oti_restore_params(rt, params);
  oti_restore_handlers(rt, handlers, handler_top);
  rt->sp = base;
  if (!result && !oti_failed(rt)) oti_error(rt, "native call failed");
  return result;
}

ot_value oti_native_invoke_list(ot_state* rt, ot_value function, ot_value args) {
  size_t n = 0;
  for (ot_value p = args; oti_kind(p, K_PAIR); p = oti_cdr(p)) n++;
  ot_value live[] = {function, args};
  oti_roots roots;
  oti_roots_push(&rt->heap, &roots, live, 2);
  bool ok = oti_native_args(rt, n);
  oti_roots_pop(&rt->heap, &roots);
  if (!ok) return 0;
  size_t i = 0;
  for (ot_value p = live[1]; oti_kind(p, K_PAIR); p = oti_cdr(p)) rt->native.args[i++] = oti_car(p);
  return oti_native_invoke(rt, live[0], n);
}

/* --- Frame walking ------------------------------------------------------ */

/* A frame in a walk from the one that last called C outwards: its stack
 * pointer, and the return address of the call it is making. */
typedef struct {
  uintptr_t sp, ra;
} oti_walk;

static oti_walk walk_from(uintptr_t sp) { return (oti_walk){sp, sp ? ((uintptr_t*)sp)[-1] : 0}; }

/* Step over entry records (to the run of frames that called C) unless
 * entries is false, and over segment links. Returns whether w is a frame. */
static bool walk_frame(oti_walk* w, bool entries) {
  while (w->sp) {
    uintptr_t* at = (uintptr_t*)w->sp;
    if (w->ra == (uintptr_t)oti_native_return) {
      if (!entries) return false;
      *w = walk_from(at[0]);
    } else if (w->ra == (uintptr_t)oti_segment_return) {
      *w = (oti_walk){at[0], at[1]};
    } else {
      return true;
    }
  }
  return false;
}

/* Move w from a frame with descriptor d to its caller. */
static void walk_next(oti_walk* w, const oti_descriptor* d) {
  w->ra = *return_slot(w->sp, d->frame);
  w->sp += frame_span(d->frame);
}

/* Visit the live slots of every native frame, run by run, from the frame
 * that last called C out to the outermost entry record. */
static void visit_frames(ot_state* rt, uintptr_t sp, oti_root_visitor visit, void* context) {
  for (oti_walk w = walk_from(sp); walk_frame(&w, true);) {
    const oti_code* c;
    const oti_descriptor* d = descriptor_at(rt->native_runtime, w.ra, &c);
    if (!d) {
      fprintf(stderr, "otium: no frame descriptor for return address %#lx\n", (unsigned long)w.ra);
      abort();
    }
    const uint64_t* bits = c->bitmaps + d->slots;
    ot_value* slots = (ot_value*)(w.sp + SLJIT_LOCALS_OFFSET);
    OTI_UNPOISON(w.sp, d->frame);
    for (uint32_t i = 0; i < d->words; i++)
      for (uint64_t word = bits[i]; word; word &= word - 1)
        visit(context, slots + i * 64 + (uint32_t)__builtin_ctzll(word));
    walk_next(&w, d);
  }
}

/* native.args is not a root: values pass through it between a call and the
 * callee's entry, or into C that copies them before it can collect. The
 * exception is a parked builtin call's (args_live). */
void oti_native_visit_exec(ot_state* rt, oti_exec* x, oti_root_visitor visit, void* context) {
  visit(context, &x->native.callee);
  for (size_t i = 0; i < x->native.args_live; i++) visit(context, &x->native.args[i]);
  visit_frames(rt, x->native.sp, visit, context);
}

/* The frames of an uncaught error, innermost first, as oti_trace_error does
 * for VM frames. */
#define TRACE_LIMIT 200

static char* copy_name(const char* s) {
  if (!s) return NULL;
  size_t n = strlen(s) + 1;
  char* copy = malloc(n);
  if (copy) memcpy(copy, s, n);
  return copy;
}

void oti_native_trace(ot_state* rt) {
  oti_error_record* e = &rt->error;
  if (!e->set || e->traced || rt->try_count || rt->interrupted || rt->exit_pending ||
      rt->restart_pending)
    return;
  e->traced = true;
  oti_error_place* places = calloc(TRACE_LIMIT, sizeof(oti_error_place));
  if (!places) return;
  size_t count = 0;
  bool seen = false;
  for (oti_walk w = walk_from(rt->native.sp); count < TRACE_LIMIT && walk_frame(&w, true);) {
    const oti_descriptor* d = descriptor_at(rt->native_runtime, w.ra, NULL);
    if (!d) break;
    walk_next(&w, d);
    if (d->function == UINT32_MAX || d->offset == UINT32_MAX) continue;
    const oti_function* fn = rt->functions[d->function];
    if (!fn || fn->source == OTI_NO_SOURCE) continue;
    bool innermost = !seen;
    seen = true;
    if (innermost && d->tail && strcmp(e->code, "E0201")) continue;
    /* Recursion through one call site shows once. */
    if (count && places[count - 1].source == fn->source && places[count - 1].offset == d->offset)
      continue;
    places[count++] = (oti_error_place){fn->source, d->offset, copy_name(fn->name)};
  }
  if (!count) {
    free(places);
    return;
  }
  e->frames = places;
  e->frame_count = count;
}

/* The source offset of the innermost native call site with one. */
static bool innermost_offset(ot_state* rt, uint32_t* source, uint32_t* offset) {
  for (oti_walk w = walk_from(rt->native.sp); walk_frame(&w, false);) {
    const oti_descriptor* d = descriptor_at(rt->native_runtime, w.ra, NULL);
    if (!d) return false;
    if (d->function != UINT32_MAX && d->offset != UINT32_MAX && rt->functions[d->function]) {
      *source = rt->functions[d->function]->source;
      *offset = d->offset;
      return true;
    }
    walk_next(&w, d);
  }
  return false;
}

/* --- Unwinding ---------------------------------------------------------- */

typedef uintptr_t W;

/* Drop cleanup saves whose stubs lie below sp, which unwinding is about to
 * leave: a cleanup that raised never came back for its save. */
static void drop_unwinds(ot_state* rt, uintptr_t sp) {
  while (rt->unwind_count && rt->unwinds[rt->unwind_count - 1].sp < sp)
    oti_release_error(&rt->unwinds[--rt->unwind_count].error);
}

static _Noreturn void resume(ot_state* rt, uintptr_t sp, uintptr_t pc, ot_value value) {
  unwind_segments(rt, sp);
  drop_unwinds(rt, sp);
  oti_native_resume(rt, sp, pc, value);
}

static _Noreturn void fail_to_entry(ot_state* rt) {
  resume(rt, rt->native.entry, (uintptr_t)oti_native_fail, 0);
}

/* Run the innermost cleanup as native code, unwinding: save what is in
 * flight, put the state back as it was when the cleanup was made, and jump
 * to the unwind stub just under the frame that called C, as if that frame
 * had called it. The stub calls the cleanup and then h_continue_unwind,
 * which puts the save back and goes on unwinding. So a cleanup has no C
 * frames under it, and its thread can wait for others. */
static _Noreturn void unwind_cleanup(ot_state* rt) {
  size_t index = rt->cleanup_count - 1;
  oti_cleanup cleanup = rt->cleanup_states[index];
  rt->native.callee = rt->cleanups[index];
  oti_restore_cleanups(rt, index);
  uintptr_t sp = call_sp(rt->native.sp);
  if (!oti_grow(rt, (void**)&rt->unwinds, &rt->unwind_capacity, rt->unwind_count + 1,
                sizeof(oti_unwind_save))) {
    /* Out of memory: skip the cleanup and keep unwinding with that error. */
    oti_native_raise(rt);
  }
  oti_unwind_save* u = &rt->unwinds[rt->unwind_count++];
  *u = (oti_unwind_save){sp,
                         rt->error,
                         rt->condition_values[0],
                         {rt->restart_transfer[0], rt->restart_transfer[1]},
                         rt->exit_status,
                         rt->raised,
                         rt->signaled,
                         rt->restart_pending,
                         rt->exit_pending,
                         rt->interrupted};
  rt->error = (oti_error_record){.slots = {OT_NIL, OT_NIL, OT_NIL, OT_NIL}};
  bool error = !u->restart && !u->exiting && !u->interrupted;
  rt->frame_count = cleanup.state.frames;
  rt->try_count = cleanup.tries;
  rt->sp = cleanup.state.sp;
  oti_restore_params(rt, cleanup.state.params);
  oti_restore_handlers(rt, cleanup.state.handlers, cleanup.state.handler_top);
  oti_restore_restarts(rt, cleanup.state.restarts);
  rt->raised = rt->signaled = rt->restart_pending = rt->exit_pending = rt->interrupted = false;
  rt->condition_values[0] = OT_NIL;
  rt->restart_transfer[0] = rt->restart_transfer[1] = OT_NIL;
  ot_value outcome = OT_NIL;
  if (error) {
    /* The cleanup sees the condition, and unwinding goes on raising it. */
    u = &rt->unwinds[rt->unwind_count - 1];
    ot_value condition = u->raised ? u->condition : oti_condition_of(rt, &u->error);
    if (!condition) {
      rt->unwind_count--;
      oti_native_raise(rt);
    }
    u = &rt->unwinds[rt->unwind_count - 1];
    u->condition = outcome = condition;
    u->raised = true;
  } else if (u->exiting || u->interrupted) {
    outcome = oti_intern(rt, K_KEYWORD, ":quit", 5);
    if (!outcome) oti_native_raise(rt);
  }
  rt->native.argc = cleanup.outcome ? 1 : 0;
  rt->native.args[0] = outcome;
  resume(rt, sp, rt->native_runtime->stubs[STUB_UNWIND], 0);
}

/* The unwind stub's cleanup returned: put back what was in flight and go on
 * unwinding. */
static _Noreturn void unwind(ot_state* rt, size_t stop);

static W h_continue_unwind(ot_state* rt) {
  oti_unwind_save u = rt->unwinds[--rt->unwind_count];
  oti_clear_error(rt);
  rt->error = u.error;
  rt->raised = u.raised;
  rt->signaled = u.signaled;
  rt->condition_values[0] = u.condition;
  rt->restart_pending = u.restart;
  rt->restart_transfer[0] = u.transfer[0];
  rt->restart_transfer[1] = u.transfer[1];
  rt->interrupted = u.interrupted;
  rt->exit_pending = u.exiting;
  rt->exit_status = u.status;
  unwind(rt, rt->frame_count - 1);
}

static bool resume_restart(ot_state* rt, size_t stop) {
  size_t index = rt->restart_count;
  while (index && rt->restarts[index - 1] != rt->restart_transfer[0]) index--;
  if (!index) {
    rt->restart_pending = false;
    return oti_error(rt, "restart is no longer active");
  }
  size_t scope_index = rt->restart_scope_count;
  while (scope_index && rt->restart_scopes[scope_index - 1].first >= index) scope_index--;
  oti_restart_scope scope = rt->restart_scopes[scope_index - 1];
  if (rt->cleanup_count > scope.state.cleanups) {
    if (rt->cleanup_states[rt->cleanup_count - 1].state.frames <= stop) fail_to_entry(rt);
    unwind_cleanup(rt);
  }
  if (scope.state.frames <= stop) fail_to_entry(rt);
  oti_restore_restarts(rt, scope_index);
  rt->try_count = scope.tries;
  oti_restore_params(rt, scope.state.params);
  oti_restore_handlers(rt, scope.state.handlers, scope.state.handler_top);
  rt->sp = scope.state.sp;
  rt->restart_pending = false;
  oti_clear_error(rt);
  rt->raised = rt->signaled = false;
  rt->condition_values[0] = OT_NIL;
  /* The target calls the restart's handler with the arguments. */
  ot_value transfer = oti_alloc(rt, K_PAIR, 2, 0);
  if (!transfer) return true;
  oti_as_pair(transfer)->car = oti_as_restart(rt->restart_transfer[0])->handler;
  oti_as_pair(transfer)->cdr = rt->restart_transfer[1];
  rt->restart_transfer[0] = rt->restart_transfer[1] = OT_NIL;
  resume(rt, scope.state.slot, scope.state.target, transfer);
}

/* Unwind from the native frame that called this C function, after an error
 * has been recorded: run handlers and cleanups as oti_drive does, then jump
 * to the innermost try or restart in this run of native frames, or return
 * failure to the trampoline that started the run. */
_Noreturn void oti_native_raise(ot_state* rt) {
  size_t stop = rt->frame_count - 1;
  if (!oti_failed(rt)) oti_error(rt, "native code failed");
  {
    uint32_t source, offset;
    oti_error_record* e = &rt->error;
    if (e->set && !e->located && innermost_offset(rt, &source, &offset) &&
        source != OTI_NO_SOURCE) {
      e->offset = offset;
      e->source = source;
      e->located = true;
    }
  }
  unwind(rt, stop);
}

static _Noreturn void unwind(ot_state* rt, size_t stop) {
  for (;;) {
    if (!rt->try_count) oti_native_trace(rt);
    if (!rt->interrupted && !rt->exit_pending && !rt->restart_pending && !rt->signaled)
      oti_signal_error(rt);
    if (rt->restart_pending) {
      resume_restart(rt, stop);
      continue;
    }
    size_t target_cleanups = !rt->interrupted && !rt->exit_pending && rt->try_count
                                 ? rt->tries[rt->try_count - 1].cleanups
                                 : 0;
    if (rt->cleanup_count > target_cleanups) {
      if (rt->cleanup_states[rt->cleanup_count - 1].state.frames <= stop) fail_to_entry(rt);
      unwind_cleanup(rt);
    }
    if (rt->interrupted || rt->exit_pending || !rt->try_count ||
        rt->tries[rt->try_count - 1].frames <= stop)
      fail_to_entry(rt);
    oti_try_frame handler = rt->tries[--rt->try_count];
    oti_restore_restarts(rt, handler.restarts);
    oti_restore_params(rt, handler.params);
    oti_restore_handlers(rt, handler.handlers, handler.handler_top);
    rt->sp = handler.sp;
    ot_value condition = oti_error_condition(rt);
    if (!condition) continue;
    oti_clear_error(rt);
    rt->raised = false;
    rt->signaled = false;
    rt->condition_values[0] = OT_NIL;
    resume(rt, handler.slot, handler.target, condition);
  }
}

/* --- Helpers ------------------------------------------------------------ */

static W h_alloc(ot_state* rt, W kind, W fields, W bytes) {
  ot_value v = oti_alloc_slow(rt, (uint32_t)kind, (uint32_t)fields, (size_t)bytes);
  if (!v) oti_native_raise(rt);
  return v;
}

static ot_value builtin_on_stack(ot_state* rt, unsigned id, ot_value a, ot_value b, size_t n) {
  size_t first = rt->sp;
  if (!oti_stack(rt, first + 2)) oti_native_raise(rt);
  rt->stack[first] = a;
  rt->stack[first + 1] = b;
  rt->sp = first + n;
  ot_value result = oti_builtin(rt, id, first, n);
  rt->sp = first;
  if (!result || oti_failed(rt)) oti_native_raise(rt);
  return result;
}

static W h_prim1(ot_state* rt, W id, W a) {
  return builtin_on_stack(rt, (unsigned)id, a, OT_NIL, 1);
}

static W h_prim2(ot_state* rt, W id, W a, W b) {
  return builtin_on_stack(rt, (unsigned)id, a, b, 2);
}

static W h_arity(ot_state* rt, W closure, W argc) {
  const oti_function* fn = oti_closure_fn(rt, closure);
  if (fn) oti_arity_error(rt, fn, argc);
  else oti_coded_error(rt, "E0301", "wrong number of arguments");
  oti_native_raise(rt);
}

static W h_unbound(ot_state* rt, W marker) {
  oti_unbound_error(rt, (uint32_t)(marker >> 8));
  oti_native_raise(rt);
}

static W h_uninitialized(ot_state* rt, W captured) {
  oti_error(rt, captured ? "uninitialized captured binding" : "uninitialized local");
  oti_native_raise(rt);
}

static bool inlined_builtin_cell(ot_state* rt, uint32_t index) {
  ot_value old = oti_global_value(rt, index);
  if ((old & 255) != TAG_BUILTIN) return false;
  unsigned id = (unsigned)(old >> 8);
  if (!oti_inlined_builtin(id) || rt->builtin_cells[id] != index) return false;
  oti_errorf(rt, "cannot redefine builtin: %s", rt->global_info[index].name);
  return true;
}

static W h_define(ot_state* rt, W cell, W value) {
  if (inlined_builtin_cell(rt, (uint32_t)cell)) oti_native_raise(rt);
  rt->global_info[cell].defined = 1;
  rt->global_info[cell].macro = oti_kind(value, K_MACRO);
  oti_set_global(rt, (uint32_t)cell, value);
  return value;
}

static W h_set_global(ot_state* rt, W cell, W value) {
  if (oti_unbound(oti_global_value(rt, (uint32_t)cell))) {
    oti_coded_error(rt, "E0201", "cannot set an unbound global");
    oti_native_raise(rt);
  }
  if (inlined_builtin_cell(rt, (uint32_t)cell)) oti_native_raise(rt);
  rt->global_info[cell].macro = oti_kind(value, K_MACRO);
  oti_set_global(rt, (uint32_t)cell, value);
  return OT_NIL;
}

/* The stack check at function entries and loop back edges failed: the stack
 * really is too deep, or an interrupt was requested. */
static W h_poll(ot_state* rt) {
  /* Past the limit only ever at a function's entry, where its frame can
   * move to a new segment. */
  if (rt->native.sp < rt->native.base_limit && !grow_frame(rt)) oti_native_raise(rt);
  if (rt->native.limit != rt->native.base_limit) {
    rt->native.limit = rt->native.base_limit;
    if (!oti_interrupt_point(rt)) oti_native_raise(rt);
    if (rt->threads && !oti_thread_poll(rt)) oti_native_raise(rt);
  }
  return OT_NIL;
}

/* The rest list of a variadic call, from native.args[fixed, argc). */
static W h_rest(ot_state* rt, W fixed) {
  ot_value rest = OT_EMPTY;
  oti_roots root, args;
  oti_roots_push(&rt->heap, &root, &rest, 1);
  oti_roots_push(&rt->heap, &args, rt->native.args, rt->native.argc);
  for (size_t i = rt->native.argc; i > fixed; i--) {
    ot_value pair = oti_alloc(rt, K_PAIR, 2, 0);
    if (!pair) {
      oti_roots_pop(&rt->heap, &args);
      oti_roots_pop(&rt->heap, &root);
      oti_native_raise(rt);
    }
    oti_as_pair(pair)->car = rt->native.args[i - 1];
    oti_as_pair(pair)->cdr = rest;
    rest = pair;
  }
  oti_roots_pop(&rt->heap, &args);
  oti_roots_pop(&rt->heap, &root);
  return rest;
}

static W h_try(ot_state* rt, W pc, W sp) {
  if (!oti_grow(rt, (void**)&rt->tries, &rt->try_capacity, rt->try_count + 1,
                sizeof(oti_try_frame)))
    oti_native_raise(rt);
  rt->tries[rt->try_count++] =
      (oti_try_frame){rt->frame_count,     rt->sp,        pc, rt->param_count, rt->handler_count,
                      rt->handler_top,     rt->restart_scope_count, rt->cleanup_count, sp};
  return OT_NIL;
}

static W h_end_try(ot_state* rt) {
  if (rt->try_count) rt->try_count--;
  return OT_NIL;
}

static W h_raise(ot_state* rt, W condition) {
  rt->condition_values[0] = condition;
  rt->raised = true;
  oti_error(rt, "uncaught condition");
  rt->signaled = true;
  oti_native_raise(rt);
}

static W h_cleanup(ot_state* rt, W closure, W outcome) {
  size_t top = rt->sp;
  if (!oti_stack(rt, top + 1)) oti_native_raise(rt);
  rt->stack[top] = closure;
  rt->sp = top + 1;
  bool ok = oti_install_cleanup(rt, outcome != OT_FALSE);
  rt->sp = top;
  if (!ok) oti_native_raise(rt);
  return OT_NIL;
}

/* Leaving a cleanup's body normally: drop its record and return the cleanup,
 * which native code calls itself. */
static W h_pop_cleanup(ot_state* rt) {
  if (!rt->cleanup_count) {
    oti_error(rt, "invalid cleanup stack");
    oti_native_raise(rt);
  }
  oti_restore_cleanups(rt, rt->cleanup_count - 1);
  return rt->cleanups[rt->cleanup_count];
}

static W h_run_cleanup(ot_state* rt) {
  if (!oti_run_cleanup(rt, false)) oti_native_raise(rt);
  return OT_NIL;
}

/* Values for handlers and restarts arrive in native.args. */
static size_t args_to_stack(ot_state* rt, size_t count) {
  size_t top = rt->sp;
  if (!oti_stack(rt, top + count)) oti_native_raise(rt);
  memcpy(rt->stack + top, rt->native.args, count * sizeof(ot_value));
  rt->sp = top + count;
  return top;
}

static W h_handlers(ot_state* rt, W count) {
  size_t top = args_to_stack(rt, 2 * count);
  bool ok = oti_install_handlers(rt, top, count);
  rt->sp = top;
  if (!ok) oti_native_raise(rt);
  return OT_NIL;
}

static W h_unhandlers(ot_state* rt, W count) {
  if (count > rt->handler_count) {
    oti_error(rt, "invalid handler stack");
    oti_native_raise(rt);
  }
  oti_remove_handlers(rt, count);
  return OT_NIL;
}

static W h_bind_param(ot_state* rt, W param, W value) {
  if (!oti_bind_param(rt, param, value)) oti_native_raise(rt);
  return OT_NIL;
}

static W h_unbind_params(ot_state* rt, W count) {
  if (count > rt->param_count) {
    oti_error(rt, "invalid parameter stack");
    oti_native_raise(rt);
  }
  oti_restore_params(rt, rt->param_count - count);
  return OT_NIL;
}

static W h_make_param(ot_state* rt, W name, W value) {
  ot_value live[] = {name, value};
  oti_roots roots;
  oti_roots_push(&rt->heap, &roots, live, 2);
  ot_value v = oti_alloc(rt, K_PARAM, 2, 0);
  oti_roots_pop(&rt->heap, &roots);
  if (!v) oti_native_raise(rt);
  oti_as_param(v)->name = live[0];
  oti_as_param(v)->value = live[1];
  return v;
}

/* Restart triples arrive in native.args; they stay on rt->stack, where the
 * scope's sp points, until the scope ends. */
static W h_restarts(ot_state* rt, W count, W pc, W sp) {
  size_t top = args_to_stack(rt, 3 * count);
  bool ok = oti_install_restarts(rt, top, count, pc);
  rt->sp = top;
  if (!ok) oti_native_raise(rt);
  rt->restart_scopes[rt->restart_scope_count - 1].state.slot = sp;
  return OT_NIL;
}

static W h_end_restarts(ot_state* rt) {
  if (!rt->restart_scope_count) {
    oti_error(rt, "invalid restart scope");
    oti_native_raise(rt);
  }
  oti_restore_restarts(rt, rt->restart_scope_count - 1);
  return OT_NIL;
}

static W h_restart_apply(ot_state* rt, W transfer) {
  ot_value result = oti_native_invoke_list(rt, oti_car(transfer), oti_cdr(transfer));
  if (!result) oti_native_raise(rt);
  return result;
}

/* The closure eval reads as, for the namespace named by a symbol. */
static W h_evaluator(ot_state* rt, W name) {
  size_t length;
  const char* text = oti_text_bytes(name, &length);
  uint32_t space = oti_namespace(rt, text, length);
  if (space == UINT32_MAX) oti_native_raise(rt);
  ot_value v = oti_evaluator(rt, space);
  if (!v) oti_native_raise(rt);
  return v;
}

/* The helpers the stream can call, by number. */
#define HELPERS(X) \
  X(H_ALLOC, h_alloc)  \
  X(H_PRIM1, h_prim1)  \
  X(H_PRIM2, h_prim2)  \
  X(H_ARITY, h_arity)  \
  X(H_UNBOUND, h_unbound)  \
  X(H_UNINITIALIZED, h_uninitialized)  \
  X(H_DEFINE, h_define)  \
  X(H_SET_GLOBAL, h_set_global)  \
  X(H_POLL, h_poll)  \
  X(H_REST, h_rest)  \
  X(H_TRY, h_try)  \
  X(H_END_TRY, h_end_try)  \
  X(H_RAISE, h_raise)  \
  X(H_CLEANUP, h_cleanup)  \
  X(H_RUN_CLEANUP, h_run_cleanup)  \
  X(H_HANDLERS, h_handlers)  \
  X(H_UNHANDLERS, h_unhandlers)  \
  X(H_BIND_PARAM, h_bind_param)  \
  X(H_UNBIND_PARAMS, h_unbind_params)  \
  X(H_MAKE_PARAM, h_make_param)  \
  X(H_RESTARTS, h_restarts)  \
  X(H_END_RESTARTS, h_end_restarts)  \
  X(H_RESTART_APPLY, h_restart_apply)  \
  X(H_EVALUATOR, h_evaluator)  \
  X(H_POP_CLEANUP, h_pop_cleanup)

enum {
#define HELPER_ID(id, fn) id,
  HELPERS(HELPER_ID)
#undef HELPER_ID
  H_COUNT
};

static W dispatch(ot_state* rt);

#define HELPER_BRIDGE(id, fn) BRIDGE(fn);
#define HELPER_DECLARE(id, fn) extern char oti_bridge_##fn[];
HELPERS(HELPER_DECLARE)
extern char oti_bridge_dispatch[], oti_bridge_h_continue_unwind[];

void oti_native_bridges(void);
void oti_native_bridges(void) {
  HELPERS(HELPER_BRIDGE)
  BRIDGE(dispatch);
  BRIDGE(h_continue_unwind);
}

static const void* helper_address(uint64_t id) {
#define HELPER_ENTRY(id, fn) [id] = (const void*)oti_bridge_##fn,
  static const void* const helpers[H_COUNT] = {HELPERS(HELPER_ENTRY)};
#undef HELPER_ENTRY
  /* The poll goes to h_poll through oti_poll, which crosses segments
   * itself. */
  if (id == H_POLL) return oti_poll;
  return id < H_COUNT ? helpers[id] : NULL;
}

/* --- Calls to everything but closures ----------------------------------- */

#define APPLY_VALUE (((ot_value)B_APPLY << 8) | TAG_BUILTIN)

/* Spread apply's final list into the call at rt->stack[call]. */
static bool spread(ot_state* rt, size_t call, size_t* argc) {
  if (*argc < 2) return oti_error(rt, "apply needs a callable and a final argument sequence");
  ot_value list = rt->stack[call + *argc];
  bool array = oti_kind(list, K_ARRAY);
  ot_value cursor = array || list == OT_NIL ? OT_EMPTY : list, slow = cursor;
  size_t length = array ? oti_as_array(list)->length : 0, prefix = *argc - 2;
  while (oti_kind(cursor, K_PAIR)) {
    if (length >= rt->stack_limit) return oti_error(rt, "VM stack limit exceeded");
    length++;
    cursor = oti_cdr(cursor);
    if (!(length & 1)) slow = oti_cdr(slow);
    if (cursor != OT_EMPTY && cursor == slow)
      return oti_error(rt, "apply needs an acyclic proper list");
  }
  if (cursor != OT_EMPTY) return oti_error(rt, "apply needs a proper list");
  size_t count = prefix + length;
  if (count > rt->stack_limit || call + 1 + count > rt->stack_limit)
    return oti_error(rt, "VM stack limit exceeded");
  if (!oti_stack(rt, call + 1 + count)) return false;
  memmove(rt->stack + call, rt->stack + call + 1, (prefix + 1) * sizeof(ot_value));
  if (array && length)
    memcpy(rt->stack + call + 1 + prefix, oti_as_array_data(oti_as_array(list)->data)->items,
           length * sizeof(ot_value));
  else
    for (size_t i = 0; i < length; i++) {
      rt->stack[call + 1 + prefix + i] = oti_car(list);
      list = oti_cdr(list);
    }
  *argc = count;
  rt->sp = call + 1 + count;
  return true;
}

/* The C half of the call_other stub: native.callee applied to native.argc
 * arguments in native.args. Returns the result, or 0 after leaving a closure
 * and its arguments in native.callee, native.argc and native.args for the
 * stub to enter by tail call. */
static W dispatch(ot_state* rt) {
  size_t argc = rt->native.argc, base = rt->sp;
  /* Builtins, the common case, go straight to their function. */
  ot_value callee = rt->native.callee;
  if ((callee & 255) == TAG_BUILTIN && callee != APPLY_VALUE && (callee >> 8) < B_COUNT &&
      base + argc <= rt->stack_room) {
    /* A new thread has no VM stack until something needs one. */
    if (argc) memcpy(rt->stack + base, rt->native.args, argc * sizeof(ot_value));
    rt->sp = base + argc;
    ot_value result = rt->builtin_run[callee >> 8](rt, base, argc);
    rt->sp = base;
    if (result == OTI_PARKED) oti_thread_wait(rt);
    if (!result || oti_failed(rt)) oti_native_raise(rt);
    return result;
  }
  if (argc + 1 > rt->stack_limit || base + 1 + argc > rt->stack_limit ||
      !oti_stack(rt, base + 1 + argc)) {
    if (!oti_failed(rt)) oti_error(rt, "VM stack limit exceeded");
    oti_native_raise(rt);
  }
  rt->stack[base] = rt->native.callee;
  memcpy(rt->stack + base + 1, rt->native.args, argc * sizeof(ot_value));
  rt->sp = base + 1 + argc;
  for (;;) {
    ot_value f = rt->stack[base];
    if (f == APPLY_VALUE) {
      if (!spread(rt, base, &argc)) oti_native_raise(rt);
      continue;
    }
    if (oti_kind(f, K_MACRO)) {
      rt->stack[base] = oti_as_macro(f)->expander;
      continue;
    }
    if (oti_kind(f, K_GENERIC) || oti_kind(f, K_NEXT)) {
      if (!oti_generic_prepare(rt, base, &argc)) oti_native_raise(rt);
      continue;
    }
    if (oti_kind(f, K_CLOSURE)) {
      if (!oti_native_args(rt, argc)) oti_native_raise(rt);
      memcpy(rt->native.args, rt->stack + base + 1, argc * sizeof(ot_value));
      rt->native.callee = rt->stack[base];
      rt->native.argc = argc;
      rt->sp = base;
      return 0;
    }
    if (oti_primitive_value(f)) {
      ot_value result = oti_call_primitive(rt, base, argc);
      rt->sp = base;
      if (result == OTI_PARKED) oti_thread_wait(rt);
      if (!result || oti_failed(rt)) oti_native_raise(rt);
      return result;
    }
    oti_callable_error(rt, f);
    oti_native_raise(rt);
  }
}

/* --- Stubs -------------------------------------------------------------- */

typedef struct {
  struct sljit_label* label;
  uint32_t frame;
} oti_stub_site;

/* The stubs are hand-written sljit: call_other, the entry of every callee
 * that isn't a closure; unbound, the entry of unbound cells; and the
 * evaluator, the code of the closures eval reads as. */
static bool build_stubs(ot_state* rt) {
  oti_native_runtime* n = rt->native_runtime;
  struct sljit_compiler* c = sljit_create_compiler(NULL);
  if (!c) return false;
  oti_stub_site sites[4];
  struct sljit_label* entries[STUB_COUNT];
  uint32_t frame;
  sljit_sw args = (sljit_sw)offsetof(ot_state, native.args);
  sljit_sw callee = (sljit_sw)offsetof(ot_state, native.callee);
  sljit_sw argc = (sljit_sw)offsetof(ot_state, native.argc);

  entries[STUB_CALL_OTHER] = sljit_emit_aligned_label(c, SLJIT_LABEL_ALIGN_16, NULL);
  enter(c, 8, 0, &frame);
  sljit_emit_op1(c, SLJIT_MOV, SLJIT_R7, 0, SLJIT_MEM1(SLJIT_S0), args);
  for (int i = 0; i < 5; i++)
    sljit_emit_op1(c, SLJIT_MOV, SLJIT_MEM1(SLJIT_R7), i * 8, SLJIT_R(2 + i), 0);
  sljit_emit_op1(c, SLJIT_MOV, SLJIT_MEM1(SLJIT_S0), callee, SLJIT_R1, 0);
  sljit_emit_op1(c, SLJIT_MOV, SLJIT_MEM1(SLJIT_S0), argc, SLJIT_R0, 0);
  /* A thread whose builtin parked comes back here, to dispatch the same
   * call again. */
  struct sljit_label* redispatch = sljit_emit_label(c);
  c_call(c, (const void*)oti_bridge_dispatch, 0);
  sites[0] = (oti_stub_site){sljit_emit_label(c), frame};
  after_c_call(c);
  struct sljit_jump* done = sljit_emit_cmp(c, SLJIT_NOT_EQUAL, SLJIT_R0, 0, SLJIT_IMM, 0);
  sljit_emit_op1(c, SLJIT_MOV, SLJIT_R7, 0, SLJIT_MEM1(SLJIT_S0), args);
  sljit_emit_op1(c, SLJIT_MOV, SLJIT_R1, 0, SLJIT_MEM1(SLJIT_S0), callee);
  sljit_emit_op1(c, SLJIT_MOV, SLJIT_R0, 0, SLJIT_MEM1(SLJIT_S0), argc);
  for (int i = 0; i < 5; i++)
    sljit_emit_op1(c, SLJIT_MOV, SLJIT_R(2 + i), 0, SLJIT_MEM1(SLJIT_R7), i * 8);
  sljit_emit_icall(c, SLJIT_CALL_REG_ARG | SLJIT_CALL_RETURN, SLJIT_ARGS0(W), SLJIT_MEM1(SLJIT_R1),
                   (sljit_sw)offsetof(oti_closure_obj, code));
  sljit_set_label(done, sljit_emit_label(c));
  sljit_emit_return(c, SLJIT_MOV, SLJIT_R0, 0);

  entries[STUB_UNBOUND] = sljit_emit_aligned_label(c, SLJIT_LABEL_ALIGN_16, NULL);
  enter(c, 7, 0, &frame);
  c_call(c, (const void*)oti_bridge_h_unbound, 1);
  sites[1] = (oti_stub_site){sljit_emit_label(c), frame};
  sljit_emit_return(c, SLJIT_MOV, SLJIT_R0, 0);

  /* (fn (form) (%eval-in-namespace namespace form)), namespace captured. */
  entries[STUB_EVALUATOR] = sljit_emit_aligned_label(c, SLJIT_LABEL_ALIGN_16, NULL);
  enter(c, 7, 0, &frame);
  sljit_emit_op1(c, SLJIT_MOV, SLJIT_R3, 0, SLJIT_R2, 0);
  sljit_emit_op1(c, SLJIT_MOV, SLJIT_R2, 0, SLJIT_MEM1(SLJIT_R1),
                 (sljit_sw)offsetof(oti_closure_obj, captures));
  sljit_emit_op1(c, SLJIT_MOV, SLJIT_R1, 0, SLJIT_IMM,
                 (sljit_sw)(((ot_value)B_EVAL_IN_NAMESPACE << 8) | TAG_BUILTIN));
  sljit_emit_op1(c, SLJIT_MOV, SLJIT_R0, 0, SLJIT_IMM, 2);
  sljit_emit_icall(c, SLJIT_CALL_REG_ARG | SLJIT_CALL_RETURN, SLJIT_ARGS0(W), SLJIT_MEM1(SLJIT_S0),
                   (sljit_sw)offsetof(ot_state, native_stubs[STUB_CALL_OTHER]));

  /* Unwinding jumps here, as if the frame that called C had called it (see
   * unwind_cleanup), with a cleanup and its arguments in native.callee,
   * native.argc and native.args. */
  entries[STUB_UNWIND] = sljit_emit_aligned_label(c, SLJIT_LABEL_ALIGN_16, NULL);
  enter(c, 8, 0, &frame);
  sljit_emit_op1(c, SLJIT_MOV, SLJIT_R7, 0, SLJIT_MEM1(SLJIT_S0), args);
  sljit_emit_op1(c, SLJIT_MOV, SLJIT_R1, 0, SLJIT_MEM1(SLJIT_S0), callee);
  sljit_emit_op1(c, SLJIT_MOV, SLJIT_R0, 0, SLJIT_MEM1(SLJIT_S0), argc);
  sljit_emit_op1(c, SLJIT_MOV, SLJIT_R2, 0, SLJIT_MEM1(SLJIT_R7), 0);
  sljit_emit_icall(c, SLJIT_CALL_REG_ARG, SLJIT_ARGS0(W), SLJIT_MEM1(SLJIT_S0),
                   (sljit_sw)offsetof(ot_state, native_stubs[STUB_CALL_OTHER]));
  sites[2] = (oti_stub_site){sljit_emit_label(c), frame};
  c_call(c, (const void*)oti_bridge_h_continue_unwind, 0);
  sites[3] = (oti_stub_site){sljit_emit_label(c), frame};
  sljit_emit_return(c, SLJIT_MOV, SLJIT_R0, 0);

  void* machine = sljit_generate_code(c, 0, n);
  if (!machine) {
    sljit_free_compiler(c);
    return false;
  }
  size_t size = sljit_get_generated_code_size(c);
  protect(machine, size, PROT_READ | PROT_EXEC);
  oti_code* code = calloc(1, sizeof(*code));
  if (!code) {
    sljit_free_compiler(c);
    return false;
  }
  code->start = (uintptr_t)machine;
  code->end = code->start + size;
  code->descriptors = calloc(4, sizeof(oti_descriptor));
  code->bitmaps = calloc(1, sizeof(uint64_t));
  if (!code->descriptors || !code->bitmaps) {
    sljit_free_compiler(c);
    free_code(code);
    return false;
  }
  for (int i = 0; i < 4; i++)
    code->descriptors[i] =
        (oti_descriptor){(uint32_t)(sljit_get_label_addr(sites[i].label) - code->start),
                         sites[i].frame, UINT32_MAX, UINT32_MAX, 0, 0, false};
  code->descriptor_count = 4;
  qsort(code->descriptors, 4, sizeof(oti_descriptor), compare_descriptors);
  n->redispatch = sljit_get_label_addr(redispatch);
  for (int i = 0; i < STUB_COUNT; i++) {
    n->stubs[i] = sljit_get_label_addr(entries[i]);
    rt->native_stubs[i] = n->stubs[i];
  }
  sljit_free_compiler(c);
  return add_code(rt, code);
}

uintptr_t oti_native_cell_entry(ot_state* rt, ot_value value) {
  if (oti_kind(value, K_CLOSURE)) return oti_as_closure(value)->code;
  if (oti_unbound(value)) return rt->native_stubs[STUB_UNBOUND];
  return rt->native_stubs[STUB_CALL_OTHER];
}

bool oti_native_evaluator(ot_state* rt, ot_value v, uint32_t space) {
  return oti_kind(v, K_CLOSURE) && oti_as_closure(v)->code == rt->native_stubs[STUB_EVALUATOR] &&
         oti_as_closure(v)->header.fields == 1 &&
         oti_as_closure(v)->captures[0] == ot_int((intptr_t)space);
}

ot_value oti_native_make_evaluator(ot_state* rt, uint32_t space) {
  ot_value v = oti_alloc_closure(rt, 1);
  if (!v) return 0;
  oti_as_closure(v)->code = rt->native_stubs[STUB_EVALUATOR];
  oti_as_closure(v)->captures[0] = ot_int((intptr_t)space);
  return v;
}

/* --- Reclamation -------------------------------------------------------- */

/* Mark the code a closure runs, and so the constants it reads. Returns the
 * constant slots to mark through mark, once per code object. */
bool oti_native_mark_closure(ot_state* rt, ot_value closure, bool (*mark)(void*, uint32_t),
                             void* context) {
  oti_code* c = code_at(rt->native_runtime, oti_as_closure(closure)->code);
  if (!c || c->marked) return true;
  c->marked = true;
  for (size_t i = 0; i < c->constant_count; i++)
    if (!mark(context, c->constants[i])) return false;
  return true;
}

/* After marking: free code objects nothing reaches, and their functions. */
void oti_native_sweep(ot_state* rt) {
  oti_native_runtime* n = rt->native_runtime;
  size_t kept = 0;
  for (size_t i = 0; i < n->code_count; i++) {
    oti_code* c = n->codes[i];
    /* Runtime stubs have no functions and are always kept. */
    if (c->marked || !c->function_count) {
      c->marked = false;
      n->codes[kept++] = c;
      continue;
    }
    for (size_t f = 0; f < c->function_count; f++) release_function(rt, c->functions[f]);
    free_code(c);
  }
  n->code_count = kept;
  memset(n->frames, 0, sizeof(n->frames));
  oti_native_stack_trim(rt, &rt->native);
}

/* --- Setup -------------------------------------------------------------- */

bool oti_native_init(ot_state* rt) {
  oti_native_runtime* n = calloc(1, sizeof(*n));
  if (!n) return oti_error(rt, "out of host memory");
  rt->native_runtime = n;
  n->verbose = getenv("OTIUM_NATIVE_VERBOSE") != NULL;
  const char* perf = getenv("OTIUM_PERF_MAP");
  if (perf && *perf && strcmp(perf, "0")) {
    char path[64];
    snprintf(path, sizeof(path), "/tmp/perf-%ld.map", (long)getpid());
    n->perf_map = fopen(path, "w");
  }
  if (!oti_native_stack_new(rt, &rt->native) || !oti_native_args(rt, 16)) return false;
  rt->native.callee = OT_NIL;
  if (!build_stubs(rt)) return oti_error(rt, "cannot build native stubs");
  rt->native_mode = true;
  /* Cells defined so far get their entries. */
  for (size_t i = 0; i < rt->global_count; i++)
    rt->globals[i].entry = oti_native_cell_entry(rt, rt->globals[i].value);
  return true;
}

void oti_native_destroy(ot_state* rt) {
  oti_native_runtime* n = rt->native_runtime;
  if (!n) return;
  for (size_t i = 0; i < n->code_count; i++) free_code(n->codes[i]);
  free(n->codes);
  for (oti_code_chunk* c = n->chunks; c;) {
    oti_code_chunk* next = c->next;
    munmap(c->base, c->size);
    free(c);
    c = next;
  }
  oti_native_stack_free(rt, &rt->native);
  while (n->segment_chunks) {
    oti_segment_chunk* chunk = n->segment_chunks;
    n->segment_chunks = chunk->next;
    munmap(chunk->base, SEGMENT_CHUNK * SEGMENT_BYTES);
    free(chunk);
  }
  if (n->perf_map) fclose(n->perf_map);
  free(n);
  free(rt->native.args);
  rt->native_runtime = NULL;
}

#endif
