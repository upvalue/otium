#include "compile.h"
#include "code.h"
#include "eval.h"

typedef struct Binding {
  u32 name;
  u32 slot;
  bool captured;
} Binding;

typedef struct Capture {
  u32 name;
  bool local;
  u32 index;
} Capture;

typedef struct LambdaInfo LambdaInfo;

OT_VEC_TYPE(Binding, VecBinding);
OT_VEC_TYPE(Capture, VecCapture);
OT_VEC_TYPE(LambdaInfo*, VecLambdaInfoPtr);

struct LambdaInfo {
  LambdaInfo* parent;
  VecBinding bindings;
  VecU32 active;
  VecCapture captures;
  VecLambdaInfoPtr children;
  u32 nfixed;
  bool hasRest;
  u32 initialCount;
  u32 userDepth;
};

static void lambda_info_init(LambdaInfo* li, LambdaInfo* parent, u32 userDepth) {
  memset(li, 0, sizeof(*li));
  li->parent = parent;
  li->userDepth = userDepth;
}

static void lambda_info_free(LambdaInfo* li);

// Mirrors the C++ destructor: delete every child, then release this node's
// vec storage (the members' own destructors in C++).
static void lambda_info_deinit(LambdaInfo* li) {
  for (u32 i = 0; i < li->children.len; i++) lambda_info_free(li->children.data[i]);
  vec_deinit(&li->bindings);
  vec_deinit(&li->active);
  vec_deinit(&li->captures);
  vec_deinit(&li->children);
}

static void lambda_info_free(LambdaInfo* li) {
  lambda_info_deinit(li);
  ot_free(li);
}

static i32 find_active(LambdaInfo* li, u32 name) {
  for (u32 i = li->active.len; i-- > 0;) {
    u32 binding = li->active.data[i];
    if (li->bindings.data[binding].name == name) return (i32)binding;
  }
  return -1;
}

static i32 find_capture(LambdaInfo* li, u32 name) {
  for (u32 i = 0; i < li->captures.len; i++)
    if (li->captures.data[i].name == name) return (i32)i;
  return -1;
}

static u32 add_binding(LambdaInfo* li, u32 name) {
  u32 index = li->bindings.len;
  vec_push(&li->bindings, ((Binding){name, index, false}));
  vec_push(&li->active, index);
  return index;
}

static i32 capture_name(LambdaInfo* li, u32 name) {
  i32 existing = find_capture(li, name);
  if (existing >= 0) return existing;
  if (!li->parent) return -1;
  i32 local = find_active(li->parent, name);
  Capture capture = {name, false, 0};
  if (local >= 0) {
    li->parent->bindings.data[(u32)local].captured = true;
    capture.local = true;
    capture.index = li->parent->bindings.data[(u32)local].slot;
  } else {
    i32 parentCapture = capture_name(li->parent, name);
    if (parentCapture < 0) return -1;
    capture.index = (u32)parentCapture;
  }
  vec_push(&li->captures, capture);
  return (i32)(li->captures.len - 1);
}

static void analyze_expr(State* vm, LambdaInfo* li, Ref form);
static void analyze_body(State* vm, LambdaInfo* li, Ref forms);

static bool symbol_is(State* vm, Ref value, u32 name) {
  return ot_tag(vm, value) == Tag_Symbol && ot_id(vm, value) == name;
}

static void strip_array_head(State* vm, Ref dst, Ref forms) {
  if (ot_tag(vm, forms) != Tag_Pair) {
    ot_copy(vm, dst, forms);
    return;
  }
  OT_SCOPE(vm);
  Ref head = ot_push(vm);
  ot_car(vm, head, forms);
  if (symbol_is(vm, head, ot_syms(vm)->array_)) ot_cdr(vm, dst, forms);
  else ot_copy(vm, dst, forms);
}

static void skip_docstring_ref(State* vm, Ref dst, Ref doc, Ref forms) {
  ot_set_nil(vm, doc);
  if (ot_tag(vm, forms) != Tag_Pair) {
    ot_copy(vm, dst, forms);
    return;
  }
  OT_SCOPE(vm);
  Ref first = ot_push(vm);
  Ref rest = ot_push(vm);
  ot_car(vm, first, forms);
  ot_cdr(vm, rest, forms);
  if (ot_tag(vm, first) == Tag_String && ot_tag(vm, rest) == Tag_Pair) {
    ot_copy(vm, doc, first);
    ot_copy(vm, dst, rest);
  } else {
    ot_copy(vm, dst, forms);
  }
}

static void defined_name(State* vm, Ref dst, Ref form) {
  OT_SCOPE(vm);
  Ref args = ot_push(vm);
  ot_cdr(vm, args, form);
  if (ot_tag(vm, args) != Tag_Pair) {
    ot_set_nil(vm, dst);
    return;
  }
  ot_car(vm, dst, args);
  if (ot_tag(vm, dst) == Tag_Pair) ot_car(vm, dst, dst);
}

static void analyze_quasiquote(State* vm, LambdaInfo* li, Ref form, u32 depth) {
  if (ot_tag(vm, form) != Tag_Pair) return;
  OT_SCOPE(vm);
  const Syms* syms = ot_syms(vm);
  Ref head = ot_push(vm);
  Ref args = ot_push(vm);
  Ref part = ot_push(vm);
  ot_car(vm, head, form);
  if (symbol_is(vm, head, syms->unquote_)) {
    ot_cdr(vm, args, form);
    if (ot_tag(vm, args) == Tag_Pair) {
      ot_car(vm, part, args);
      if (depth == 1) analyze_expr(vm, li, part);
      else analyze_quasiquote(vm, li, part, depth - 1);
    }
    return;
  }
  if (symbol_is(vm, head, syms->quasiquote_)) {
    ot_cdr(vm, args, form);
    if (ot_tag(vm, args) == Tag_Pair) {
      ot_car(vm, part, args);
      analyze_quasiquote(vm, li, part, depth + 1);
    }
    return;
  }
  bool splice = false;
  if (ot_tag(vm, head) == Tag_Pair && depth == 1) {
    ot_car(vm, part, head);
    splice = symbol_is(vm, part, syms->unquoteSplicing_);
  }
  if (splice) {
    ot_cdr(vm, args, head);
    if (ot_tag(vm, args) == Tag_Pair) {
      ot_car(vm, part, args);
      analyze_expr(vm, li, part);
    }
  } else {
    analyze_quasiquote(vm, li, head, depth);
  }
  ot_cdr(vm, part, form);
  analyze_quasiquote(vm, li, part, depth);
}

static bool is_define_head(State* vm, u32 name) {
  const Syms* syms = ot_syms(vm);
  return name == syms->define_ || name == syms->def_ || name == syms->definePriv_;
}

static LambdaInfo* add_compiler_thunk(LambdaInfo* parent) {
  LambdaInfo* child = (LambdaInfo*)ot_alloc(sizeof(LambdaInfo));
  lambda_info_init(child, parent, parent->userDepth);
  vec_push(&parent->children, child);
  return child;
}

static void analyze_thunk_expr(State* vm, LambdaInfo* parent, Ref form) {
  LambdaInfo* child = add_compiler_thunk(parent);
  OT_SCOPE(vm);
  Ref part = ot_push(vm);
  if (child->userDepth > 0 && ot_tag(vm, form) == Tag_Pair) {
    ot_car(vm, part, form);
    u32 head = ot_tag(vm, part) == Tag_Symbol ? ot_id(vm, part) : 0;
    if (is_define_head(vm, head)) {
      defined_name(vm, part, form);
      if (ot_tag(vm, part) == Tag_Symbol) add_binding(child, ot_id(vm, part));
    }
  }
  child->initialCount = child->bindings.len;
  analyze_expr(vm, child, form);
}

static void analyze_thunk_body(State* vm, LambdaInfo* parent, Ref forms) {
  LambdaInfo* child = add_compiler_thunk(parent);
  analyze_body(vm, child, forms);
}

static void analyze_binding_control(State* vm, LambdaInfo* li, Ref args, bool thunkBindings) {
  if (ot_tag(vm, args) != Tag_Pair) return;
  OT_SCOPE(vm);
  Ref bindings = ot_push(vm);
  Ref cursor = ot_push(vm);
  Ref binding = ot_push(vm);
  Ref part = ot_push(vm);
  ot_car(vm, bindings, args);
  strip_array_head(vm, bindings, bindings);
  ot_copy(vm, cursor, bindings);
  while (ot_tag(vm, cursor) == Tag_Pair) {
    ot_car(vm, binding, cursor);
    if (ot_tag(vm, binding) == Tag_Pair) {
      ot_car(vm, part, binding);
      if (thunkBindings) analyze_thunk_expr(vm, li, part);
      else analyze_expr(vm, li, part);
      ot_cdr(vm, part, binding);
      if (ot_tag(vm, part) == Tag_Pair) {
        ot_car(vm, part, part);
        if (thunkBindings) analyze_thunk_expr(vm, li, part);
        else analyze_expr(vm, li, part);
      }
    }
    ot_cdr(vm, cursor, cursor);
  }
  ot_cdr(vm, part, args);
  analyze_thunk_body(vm, li, part);
}

static void analyze_one_arg_lambda(State* vm, LambdaInfo* parent, Ref param, Ref body) {
  LambdaInfo* child = (LambdaInfo*)ot_alloc(sizeof(LambdaInfo));
  lambda_info_init(child, parent, parent->userDepth + 1);
  vec_push(&parent->children, child);
  if (ot_tag(vm, param) == Tag_Symbol) {
    add_binding(child, ot_id(vm, param));
    child->nfixed = 1;
  }
  analyze_body(vm, child, body);
}

static bool parse_params(State* vm, LambdaInfo* li, Ref params) {
  if (ot_tag(vm, params) == Tag_Symbol) {
    li->hasRest = true;
    add_binding(li, ot_id(vm, params));
    return true;
  }
  OT_SCOPE(vm);
  Ref cursor = ot_push_copy(vm, params);
  Ref param = ot_push(vm);
  strip_array_head(vm, cursor, cursor);
  while (ot_tag(vm, cursor) == Tag_Pair) {
    ot_car(vm, param, cursor);
    if (symbol_is(vm, param, ot_syms(vm)->amp_)) {
      ot_cdr(vm, cursor, cursor);
      if (ot_tag(vm, cursor) != Tag_Pair) return false;
      ot_car(vm, param, cursor);
      if (ot_tag(vm, param) != Tag_Symbol) return false;
      li->hasRest = true;
      add_binding(li, ot_id(vm, param));
      ot_cdr(vm, cursor, cursor);
      return ot_tag(vm, cursor) == Tag_Null;
    }
    if (ot_tag(vm, param) != Tag_Symbol) return false;
    add_binding(li, ot_id(vm, param));
    li->nfixed++;
    ot_cdr(vm, cursor, cursor);
  }
  if (ot_tag(vm, cursor) == Tag_Symbol) {
    li->hasRest = true;
    add_binding(li, ot_id(vm, cursor));
    return true;
  }
  return ot_tag(vm, cursor) == Tag_Null;
}

// The name a body form hoists to a local slot, or nil when it is not one of
// the defining forms. Both passes walk bodies through this so they cannot
// disagree about which forms allocate a slot.
static void body_define_name(State* vm, Ref dst, Ref form) {
  if (ot_tag(vm, form) != Tag_Pair) {
    ot_set_nil(vm, dst);
    return;
  }
  OT_SCOPE(vm);
  Ref head = ot_push(vm);
  ot_car(vm, head, form);
  if (ot_tag(vm, head) != Tag_Symbol || !is_define_head(vm, ot_id(vm, head))) {
    ot_set_nil(vm, dst);
    return;
  }
  defined_name(vm, dst, form);
  if (ot_tag(vm, dst) != Tag_Symbol) ot_set_nil(vm, dst);
}

static void collect_body_defines(State* vm, LambdaInfo* li, Ref forms) {
  if (li->userDepth == 0) return;
  OT_SCOPE(vm);
  Ref cursor = ot_push_copy(vm, forms);
  Ref form = ot_push(vm);
  Ref name = ot_push(vm);
  while (ot_tag(vm, cursor) == Tag_Pair) {
    ot_car(vm, form, cursor);
    body_define_name(vm, name, form);
    if (!ot_nil(vm, name) && find_active(li, ot_id(vm, name)) < 0)
      add_binding(li, ot_id(vm, name));
    ot_cdr(vm, cursor, cursor);
  }
}

static void analyze_body(State* vm, LambdaInfo* li, Ref forms) {
  collect_body_defines(vm, li, forms);
  li->initialCount = li->bindings.len;
  OT_SCOPE(vm);
  Ref cursor = ot_push_copy(vm, forms);
  Ref form = ot_push(vm);
  while (ot_tag(vm, cursor) == Tag_Pair) {
    ot_car(vm, form, cursor);
    analyze_expr(vm, li, form);
    ot_cdr(vm, cursor, cursor);
  }
}

static void analyze_lambda(State* vm, LambdaInfo* parent, Ref params, Ref body) {
  LambdaInfo* child = (LambdaInfo*)ot_alloc(sizeof(LambdaInfo));
  lambda_info_init(child, parent, parent->userDepth + 1);
  vec_push(&parent->children, child);
  if (!parse_params(vm, child, params)) return;
  analyze_body(vm, child, body);
}

static void analyze_expr(State* vm, LambdaInfo* li, Ref form) {
  if (ot_tag(vm, form) == Tag_Symbol) {
    u32 id = ot_id(vm, form);
    if (!ot_sym_qualified(vm, id) && find_active(li, id) < 0) (void)capture_name(li, id);
    return;
  }
  if (ot_tag(vm, form) != Tag_Pair) return;
  OT_SCOPE(vm);
  const Syms* syms = ot_syms(vm);
  Ref head = ot_push(vm);
  Ref args = ot_push(vm);
  Ref part = ot_push(vm);
  Ref rest = ot_push(vm);
  Ref cursor = ot_push(vm);
  Ref clause = ot_push(vm);
  Ref scratch = ot_push(vm);
  ot_car(vm, head, form);
  ot_cdr(vm, args, form);
  if (ot_tag(vm, head) == Tag_Symbol) {
    u32 name = ot_id(vm, head);
    if (name == syms->quote_) return;
    if (name == syms->quasiquote_) {
      if (ot_tag(vm, args) == Tag_Pair) {
        ot_car(vm, part, args);
        analyze_quasiquote(vm, li, part, 1);
      }
      return;
    }
    if (name == syms->lambda_ || name == syms->fn_) {
      if (ot_tag(vm, args) == Tag_Pair) {
        ot_car(vm, part, args);
        ot_cdr(vm, rest, args);
        analyze_lambda(vm, li, part, rest);
      }
      return;
    }
    if (is_define_head(vm, name)) {
      if (ot_tag(vm, args) != Tag_Pair) return;
      ot_car(vm, part, args);
      ot_cdr(vm, rest, args);
      if (ot_tag(vm, part) == Tag_Pair) {
        ot_cdr(vm, part, part);
        analyze_lambda(vm, li, part, rest);
      }
      else {
        skip_docstring_ref(vm, rest, scratch, rest);
        if (ot_tag(vm, rest) == Tag_Pair) {
          ot_car(vm, part, rest);
          analyze_expr(vm, li, part);
        }
      }
      return;
    }
    if (name == syms->defmacro_) {
      if (ot_tag(vm, args) == Tag_Pair) {
        ot_cdr(vm, rest, args);
        if (ot_tag(vm, rest) == Tag_Pair) {
          ot_car(vm, part, rest);
          ot_cdr(vm, rest, rest);
          analyze_lambda(vm, li, part, rest);
        }
      }
      return;
    }
    if (name == syms->setBang_) {
      if (ot_tag(vm, args) == Tag_Pair) {
        ot_car(vm, part, args);
        if (ot_tag(vm, part) == Tag_Symbol) {
          u32 id = ot_id(vm, part);
          if (!ot_sym_qualified(vm, id) && find_active(li, id) < 0) (void)capture_name(li, id);
        }
        ot_cdr(vm, rest, args);
        if (ot_tag(vm, rest) == Tag_Pair) {
          ot_car(vm, part, rest);
          analyze_expr(vm, li, part);
        }
      }
      return;
    }
    if (name == syms->let_) {
      if (ot_tag(vm, args) != Tag_Pair) return;
      u32 activeBase = li->active.len;
      ot_car(vm, cursor, args);
      strip_array_head(vm, cursor, cursor);
      while (ot_tag(vm, cursor) == Tag_Pair) {
        ot_car(vm, clause, cursor);
        if (ot_tag(vm, clause) == Tag_Pair) {
          ot_car(vm, part, clause);
          ot_cdr(vm, rest, clause);
          if (ot_tag(vm, rest) == Tag_Pair) {
            ot_car(vm, scratch, rest);
            analyze_expr(vm, li, scratch);
            if (ot_tag(vm, part) == Tag_Symbol) add_binding(li, ot_id(vm, part));
          }
        }
        ot_cdr(vm, cursor, cursor);
      }
      ot_cdr(vm, cursor, args);
      collect_body_defines(vm, li, cursor);
      while (ot_tag(vm, cursor) == Tag_Pair) {
        ot_car(vm, part, cursor);
        analyze_expr(vm, li, part);
        ot_cdr(vm, cursor, cursor);
      }
      li->active.len = activeBase;
      return;
    }
    if (name == syms->ns_ || name == syms->inNs_ || name == syms->require_) return;
    if (name == syms->handlerBind_) {
      analyze_binding_control(vm, li, args, false);
      return;
    }
    if (name == syms->restartCase_) {
      if (ot_tag(vm, args) != Tag_Pair) return;
      ot_car(vm, part, args);
      analyze_thunk_expr(vm, li, part);
      ot_cdr(vm, cursor, args);
      while (ot_tag(vm, cursor) == Tag_Pair) {
        ot_car(vm, clause, cursor);
        if (ot_tag(vm, clause) == Tag_Pair) {
          ot_cdr(vm, rest, clause);
          skip_docstring_ref(vm, rest, scratch, rest);
          if (ot_tag(vm, rest) == Tag_Pair) {
            ot_car(vm, part, rest);
            ot_cdr(vm, rest, rest);
            analyze_lambda(vm, li, part, rest);
          }
        }
        ot_cdr(vm, cursor, cursor);
      }
      return;
    }
    if (name == syms->try_) {
      ot_copy(vm, cursor, args);
      while (ot_tag(vm, cursor) == Tag_Pair) {
        ot_car(vm, part, cursor);
        if (ot_tag(vm, part) == Tag_Pair) {
          ot_car(vm, scratch, part);
          if (symbol_is(vm, scratch, syms->catch_)) break;
        }
        analyze_thunk_expr(vm, li, part);
        ot_cdr(vm, cursor, cursor);
      }
      while (ot_tag(vm, cursor) == Tag_Pair) {
        ot_car(vm, clause, cursor);
        if (ot_tag(vm, clause) == Tag_Pair) {
          ot_cdr(vm, rest, clause);
          if (ot_tag(vm, rest) == Tag_Pair) {
            ot_car(vm, part, rest);
            if (ot_tag(vm, part) == Tag_Pair) {
              ot_car(vm, scratch, part);
              analyze_thunk_expr(vm, li, scratch);
              ot_cdr(vm, part, part);
              if (ot_tag(vm, part) == Tag_Pair) {
                ot_car(vm, scratch, part);
                ot_cdr(vm, rest, rest);
                analyze_one_arg_lambda(vm, li, scratch, rest);
              }
            }
          }
        }
        ot_cdr(vm, cursor, cursor);
      }
      return;
    }
    if (name == syms->unwindProtect_ || name == syms->defer_) {
      ot_copy(vm, cursor, args);
      while (ot_tag(vm, cursor) == Tag_Pair) {
        ot_car(vm, part, cursor);
        analyze_thunk_expr(vm, li, part);
        ot_cdr(vm, cursor, cursor);
      }
      return;
    }
    if (name == syms->withParams_) {
      analyze_binding_control(vm, li, args, true);
      return;
    }
    if (name == syms->defparam_) {
      if (ot_tag(vm, args) != Tag_Pair) return;
      ot_cdr(vm, rest, args);
      skip_docstring_ref(vm, rest, scratch, rest);
      if (ot_tag(vm, rest) == Tag_Pair) {
        ot_car(vm, part, rest);
        analyze_expr(vm, li, part);
      }
      return;
    }
  }
  ot_copy(vm, cursor, form);
  while (ot_tag(vm, cursor) == Tag_Pair) {
    ot_car(vm, part, cursor);
    analyze_expr(vm, li, part);
    ot_cdr(vm, cursor, cursor);
  }
}

typedef enum ResolvedKind : u8 {
  ResolvedKind_Local,
  ResolvedKind_Upval,
  ResolvedKind_Global
} ResolvedKind;
typedef struct Resolved {
  ResolvedKind kind;
  u32 index;
  bool boxed;
} Resolved;

typedef struct Compiler {
  State* vm;
  LambdaInfo* info;
  Buf bytes;
  Ref constants;
  VecU32 active;
  u32 bindingCursor;
  u32 childCursor;
  u32 depth;
  u32 maxDepth;
  bool failed;
} Compiler;

static void compiler_init(Compiler* c, State* vm, LambdaInfo* info, Ref constants) {
  c->vm = vm;
  c->info = info;
  c->bytes = (Buf){0};
  c->constants = constants;
  c->active = (VecU32){0};
  c->bindingCursor = info->initialCount;
  c->childCursor = 0;
  c->depth = 0;
  c->maxDepth = 0;
  c->failed = false;
  for (u32 i = 0; i < info->initialCount; i++) vec_push(&c->active, i);
}

// Releases the members whose C++ destructors ran when the Compiler left scope.
static void compiler_deinit(Compiler* c) {
  buf_deinit(&c->bytes);
  vec_deinit(&c->active);
}

static void compiler_error(Compiler* c, const char* message) {
  if (!c->failed) (void)raise_error(c->vm, "compile: %s", message);
  c->failed = true;
}

static void push_depth(Compiler* c) {
  c->depth++;
  if (c->depth > c->maxDepth) c->maxDepth = c->depth;
}

static void pop_depth(Compiler* c, u32 count) {
  if (count > c->depth) {
    compiler_error(c, "internal stack accounting underflow");
    c->depth = 0;
  } else {
    c->depth -= count;
  }
}

static void emit_op(Compiler* c, Op op) { vec_push(&c->bytes, (char)(u8)op); }
static void emit_u16(Compiler* c, u32 value) {
  if (value > UINT16_MAX) {
    compiler_error(c, "operand exceeds 16 bits");
    value = 0;
  }
  vec_push(&c->bytes, (char)(value & 0xff));
  vec_push(&c->bytes, (char)((value >> 8) & 0xff));
}
static void emit_i32(Compiler* c, i32 value) {
  u32 bits = (u32)value;
  for (u32 i = 0; i < 4; i++) vec_push(&c->bytes, (char)((bits >> (i * 8)) & 0xff));
}
static u32 emit_jump(Compiler* c, Op op) {
  emit_op(c, op);
  u32 operand = c->bytes.len;
  emit_i32(c, 0);
  return operand;
}
static void patch_jump(Compiler* c, u32 operand, u32 target) {
  i64 relative = (i64)target - (i64)(operand + 4);
  if (relative < INT32_MIN || relative > INT32_MAX) {
    compiler_error(c, "jump exceeds 32 bits");
    return;
  }
  u32 bits = (u32)(i32)relative;
  for (u32 i = 0; i < 4; i++) c->bytes.data[operand + i] = (char)(bits >> (i * 8));
}

// The pool grows by array_push, which allocates, so the candidate must already
// be rooted: taking a Ref makes passing a transient like car_(form) impossible.
static u32 add_constant(Compiler* c, Ref value) {
  OT_SCOPE(c->vm);
  Ref item = ot_push(c->vm);
  u32 len = ot_array_len(c->vm, c->constants);
  for (u32 i = 0; i < len; i++) {
    ot_array_get(c->vm, item, c->constants, i);
    if (ot_eq(c->vm, item, value)) return i;
  }
  if (len >= UINT16_MAX) {
    compiler_error(c, "too many constants");
    return 0;
  }
  u32 index = ot_array_len(c->vm, c->constants);
  ot_array_push(c->vm, c->constants, value);
  return index;
}

// Immediates carry no heap pointer, so they need no rooting. Separate entry
// point rather than an overload so the assert catches a heap value arriving
// through the door that does not root it.
static u32 add_constant_imm(Compiler* c, Value immediate) {
  OT_SCOPE(c->vm);
  return add_constant(c, ot_push_im(c->vm, immediate));
}

static Resolved resolve(Compiler* c, u32 name) {
  if (!ot_sym_qualified(c->vm, name)) {
    for (u32 i = c->active.len; i-- > 0;) {
      Binding* binding = &c->info->bindings.data[c->active.data[i]];
      if (binding->name == name)
        return (Resolved){ResolvedKind_Local, binding->slot, binding->captured};
    }
    i32 capture = find_capture(c->info, name);
    if (capture >= 0) return (Resolved){ResolvedKind_Upval, (u32)capture, true};
  }
  OT_SCOPE(c->vm);
  Ref constant = ot_push(c->vm);
  Ref var = ot_push(c->vm);
  ot_set_symbol(c->vm, constant, name);
  if (ot_resolve_var(c->vm, var, constant)) ot_copy(c->vm, constant, var);
  return (Resolved){ResolvedKind_Global, add_constant(c, constant), false};
}

static bool emit_expr(Compiler* c, Ref form, bool tail);

static void emit_load(Compiler* c, Resolved resolved) {
  switch (resolved.kind) {
    case ResolvedKind_Local:
      emit_op(c, resolved.boxed ? Op_GetBoxed : Op_GetLocal);
      emit_u16(c, resolved.index);
      break;
    case ResolvedKind_Upval:
      emit_op(c, Op_GetUpval);
      emit_u16(c, resolved.index);
      break;
    case ResolvedKind_Global:
      emit_op(c, Op_GetGlobal);
      emit_u16(c, resolved.index);
      break;
  }
  push_depth(c);
}

static void emit_store(Compiler* c, Resolved resolved) {
  switch (resolved.kind) {
    case ResolvedKind_Local:
      emit_op(c, resolved.boxed ? Op_SetBoxed : Op_SetLocal);
      emit_u16(c, resolved.index);
      break;
    case ResolvedKind_Upval:
      emit_op(c, Op_SetUpval);
      emit_u16(c, resolved.index);
      break;
    case ResolvedKind_Global:
      emit_op(c, Op_SetGlobal);
      emit_u16(c, resolved.index);
      break;
  }
}

static bool emit_body(Compiler* c, Ref forms, bool tail) {
  State* vm = c->vm;
  OT_SCOPE(vm);
  Ref cursor = ot_push_copy(vm, forms);
  if (ot_tag(vm, cursor) != Tag_Pair) {
    emit_op(c, Op_Nil);
    push_depth(c);
    return true;
  }
  // One reused slot for the form being emitted rather than a push per
  // iteration, so the scope's depth does not track the length of the body.
  Ref item = ot_push(vm);
  Ref rest = ot_push(vm);
  ot_cdr(vm, rest, cursor);
  while (ot_tag(vm, rest) == Tag_Pair) {
    ot_car(vm, item, cursor);
    if (!emit_expr(c, item, false)) return false;
    emit_op(c, Op_Pop);
    pop_depth(c, 1);
    ot_copy(vm, cursor, rest);
    ot_cdr(vm, rest, cursor);
  }
  ot_car(vm, item, cursor);
  return emit_expr(c, item, tail);
}

static Value compile_lambda(State* vm, LambdaInfo* info, Ref dst, Ref body, u32 name);

// The parameter list is not passed down: parse_params already recorded the
// arity and the formals' slots on `child` during analysis.
static bool emit_lambda(Compiler* c, Ref body, u32 name) {
  if (c->childCursor >= c->info->children.len) {
    compiler_error(c, "lambda analysis mismatch");
    return true;
  }
  LambdaInfo* child = c->info->children.data[c->childCursor++];
  State* vm = c->vm;
  OT_SCOPE(vm);
  Ref nested = ot_push(vm);
  Value status = compile_lambda(vm, child, nested, body, name);
  if (status.tag == Tag_Unwind) {
    c->failed = true;
    return true;
  }
  Ref descriptor = ot_push(vm);
  ot_make_array(vm, descriptor, child->captures.len + 1);
  ot_array_push(vm, descriptor, nested);
  for (u32 i = 0; i < child->captures.len; i++) {
    Capture capture = child->captures.data[i];
    i64 encoded = capture.local ? (i64)capture.index : -(i64)capture.index - 1;
    ot_array_push_im(vm, descriptor, int_v(encoded));
  }
  u32 constant = add_constant(c, descriptor);
  emit_op(c, Op_Closure);
  emit_u16(c, constant);
  push_depth(c);
  return true;
}

static bool emit_if(Compiler* c, Ref args, bool tail) {
  State* vm = c->vm;
  if (ot_tag(vm, args) != Tag_Pair) {
    compiler_error(c, "bad if");
    return true;
  }
  OT_SCOPE(vm);
  Ref part = ot_push(vm);
  Ref rest = ot_push(vm);
  ot_car(vm, part, args);
  ot_cdr(vm, rest, args);
  if (ot_tag(vm, rest) != Tag_Pair) {
    compiler_error(c, "bad if");
    return true;
  }
  if (!emit_expr(c, part, false)) return false;
  u32 branchDepth = c->depth - 1;
  u32 falseJump = emit_jump(c, Op_JumpFalse);
  pop_depth(c, 1);
  ot_car(vm, part, rest);
  bool thenFalls = emit_expr(c, part, tail);
  u32 thenDepth = c->depth;
  u32 endJump = 0;
  if (thenFalls) endJump = emit_jump(c, Op_Jump);
  u32 elseStart = c->bytes.len;
  patch_jump(c, falseJump, elseStart);
  c->depth = branchDepth;
  bool elseFalls;
  ot_cdr(vm, rest, rest);
  if (ot_tag(vm, rest) == Tag_Pair) {
    ot_car(vm, part, rest);
    elseFalls = emit_expr(c, part, tail);
  } else {
    emit_op(c, Op_Nil);
    push_depth(c);
    elseFalls = true;
  }
  u32 elseDepth = c->depth;
  u32 end = c->bytes.len;
  if (thenFalls) patch_jump(c, endJump, end);
  if (thenFalls && elseFalls && thenDepth != elseDepth)
    compiler_error(c, "if branches have different stack depths");
  c->depth = thenFalls ? thenDepth : elseDepth;
  return thenFalls || elseFalls;
}

static bool active_has(Compiler* c, u32 name) {
  for (u32 i = c->active.len; i-- > 0;)
    if (c->info->bindings.data[c->active.data[i]].name == name) return true;
  return false;
}

// Consume the binding the analysis pass recorded next and store the value on
// top of the stack into its slot. `name` is the binding this emit site expects;
// the two passes walk the same forms in the same order, so a mismatch is a
// compiler bug and must fail loudly rather than miscompile silently.
static bool bind_next_slot(Compiler* c, u32 name) {
  if (c->bindingCursor >= c->info->bindings.len) {
    compiler_error(c, "binding was not analyzed");
    return false;
  }
  Binding* metadata = &c->info->bindings.data[c->bindingCursor];
  if (metadata->name != name) {
    compiler_error(c, "analysis and emit passes disagree on binding order");
    return false;
  }
  if (metadata->captured) emit_op(c, Op_MakeBox);
  emit_op(c, Op_SetLocal);
  emit_u16(c, metadata->slot);
  emit_op(c, Op_Pop);
  pop_depth(c, 1);
  vec_push(&c->active, c->bindingCursor++);
  return true;
}

static bool emit_let(Compiler* c, Ref args, bool tail) {
  State* vm = c->vm;
  if (ot_tag(vm, args) != Tag_Pair) {
    compiler_error(c, "bad let");
    return true;
  }
  OT_SCOPE(vm);
  Ref bindings = ot_push(vm);
  ot_car(vm, bindings, args);
  strip_array_head(vm, bindings, bindings);
  u32 activeBase = c->active.len;
  Ref bindingRoot = ot_push(vm);
  Ref init = ot_push(vm);
  Ref name = ot_push(vm);
  Ref rest = ot_push(vm);
  while (ot_tag(vm, bindings) == Tag_Pair) {
    ot_car(vm, bindingRoot, bindings);
    if (ot_tag(vm, bindingRoot) != Tag_Pair) {
      compiler_error(c, "bad let binding");
      break;
    }
    ot_car(vm, name, bindingRoot);
    ot_cdr(vm, rest, bindingRoot);
    if (ot_tag(vm, name) != Tag_Symbol || ot_tag(vm, rest) != Tag_Pair) {
      compiler_error(c, "bad let binding");
      break;
    }
    ot_car(vm, init, rest);
    if (!emit_expr(c, init, false)) break;
    if (!bind_next_slot(c, ot_id(vm, name))) break;
    ot_cdr(vm, bindings, bindings);
  }
  // Mirror the analyzer's collect_body_defines for this let body: allocate a
  // nil-initialized (boxed if captured) slot per hoisted define, in the same
  // order, so bindingCursor stays in lockstep.
  if (c->info->userDepth > 0) {
    Ref body = ot_push(vm);
    ot_cdr(vm, body, args);
    Ref form = ot_push(vm);
    while (ot_tag(vm, body) == Tag_Pair) {
      ot_car(vm, form, body);
      body_define_name(vm, name, form);
      if (!ot_nil(vm, name) && !active_has(c, ot_id(vm, name))) {
        emit_op(c, Op_Nil);
        push_depth(c);
        if (!bind_next_slot(c, ot_id(vm, name))) break;
      }
      ot_cdr(vm, body, body);
    }
  }
  Ref bodyForms = ot_push(vm);
  ot_cdr(vm, bodyForms, args);
  bool falls = emit_body(c, bodyForms, tail);
  c->active.len = activeBase;
  return falls;
}

static void emit_def_global(Compiler* c, u32 name, bool isPrivate, Ref doc) {
  State* vm = c->vm;
  OT_SCOPE(vm);
  Ref descriptor = ot_push(vm);
  ot_make_array(vm, descriptor, 3);
  ot_array_push_im(vm, descriptor, symbol_v(name));
  ot_array_push_im(vm, descriptor, bool_v(isPrivate));
  ot_array_push(vm, descriptor, doc);
  emit_op(c, Op_DefGlobal);
  emit_u16(c, add_constant(c, descriptor));
}

static bool emit_define(Compiler* c, Ref form, bool isPrivate) {
  State* vm = c->vm;
  OT_SCOPE(vm);
  Ref args = ot_push(vm);
  ot_cdr(vm, args, form);
  if (ot_tag(vm, args) != Tag_Pair) {
    compiler_error(c, "bad define");
    return true;
  }
  Ref target = ot_push(vm);
  Ref name = ot_push(vm);
  ot_car(vm, target, args);
  if (ot_tag(vm, target) == Tag_Pair) ot_car(vm, name, target);
  else ot_copy(vm, name, target);
  if (ot_tag(vm, name) != Tag_Symbol) {
    compiler_error(c, "define name must be a symbol");
    return true;
  }
  u32 nameId = ot_id(vm, name);
  Ref rest = ot_push(vm);
  Ref doc = ot_push(vm);
  ot_cdr(vm, rest, args);
  if (ot_tag(vm, target) == Tag_Pair) {
    if (!emit_lambda(c, rest, nameId)) return false;
  } else {
    skip_docstring_ref(vm, rest, doc, rest);
    if (ot_tag(vm, rest) != Tag_Pair) {
      compiler_error(c, "define is missing a value");
      return true;
    }
    ot_car(vm, rest, rest);
    if (!emit_expr(c, rest, false)) return false;
  }

  if (c->info->userDepth > 0) {
    Resolved local = resolve(c, nameId);
    if (local.kind == ResolvedKind_Global) {
      compiler_error(c, "internal define was not hoisted");
      return true;
    }
    emit_store(c, local);
  } else {
    ot_cdr(vm, rest, args);
    skip_docstring_ref(vm, rest, doc, rest);
    emit_def_global(c, nameId, isPrivate, doc);
  }
  return true;
}

static bool emit_defmacro(Compiler* c, Ref form) {
  State* vm = c->vm;
  OT_SCOPE(vm);
  Ref args = ot_push(vm);
  Ref name = ot_push(vm);
  Ref rest = ot_push(vm);
  ot_cdr(vm, args, form);
  if (ot_tag(vm, args) != Tag_Pair) {
    compiler_error(c, "bad defmacro");
    return true;
  }
  ot_car(vm, name, args);
  ot_cdr(vm, rest, args);
  if (ot_tag(vm, name) != Tag_Symbol || ot_tag(vm, rest) != Tag_Pair) {
    compiler_error(c, "bad defmacro");
    return true;
  }
  u32 nameId = ot_id(vm, name);
  Ref body = ot_push(vm);
  ot_cdr(vm, body, rest);
  if (!emit_lambda(c, body, nameId)) return false;
  emit_op(c, Op_ToMacro);

  Ref doc = ot_push(vm);
  skip_docstring_ref(vm, body, doc, body);
  emit_def_global(c, nameId, false, doc);
  return true;
}

static bool finish_control_call(Compiler* c, u32 argc, bool tail) {
  emit_op(c, tail ? Op_TailCall : Op_Call);
  emit_u16(c, argc);
  pop_depth(c, argc);
  return !tail;
}

static bool emit_call(Compiler* c, Ref form, bool tail) {
  State* vm = c->vm;
  OT_SCOPE(vm);
  Ref cursor = ot_push_copy(vm, form);
  Ref item = ot_push(vm);
  ot_car(vm, item, cursor);
  if (!emit_expr(c, item, false)) return false;
  ot_cdr(vm, cursor, cursor);
  u32 argc = 0;
  while (ot_tag(vm, cursor) == Tag_Pair) {
    ot_car(vm, item, cursor);
    if (!emit_expr(c, item, false)) return false;
    argc++;
    ot_cdr(vm, cursor, cursor);
  }
  if (ot_tag(vm, cursor) != Tag_Null) {
    compiler_error(c, "dotted call");
    return true;
  }
  return finish_control_call(c, argc, tail);
}

static bool emit_while(Compiler* c, Ref args) {
  State* vm = c->vm;
  if (ot_tag(vm, args) != Tag_Pair) {
    compiler_error(c, "bad while");
    return true;
  }
  OT_SCOPE(vm);
  u32 start = c->bytes.len;
  Ref item = ot_push(vm);
  ot_car(vm, item, args);
  if (!emit_expr(c, item, false)) return false;
  u32 exit = emit_jump(c, Op_JumpFalse);
  pop_depth(c, 1);
  Ref body = ot_push(vm);
  ot_cdr(vm, body, args);
  while (ot_tag(vm, body) == Tag_Pair) {
    ot_car(vm, item, body);
    if (!emit_expr(c, item, false)) return false;
    emit_op(c, Op_Pop);
    pop_depth(c, 1);
    ot_cdr(vm, body, body);
  }
  emit_op(c, Op_Loop);
  u32 operand = c->bytes.len;
  emit_i32(c, (i32)((i64)start - (i64)(operand + 4)));
  patch_jump(c, exit, c->bytes.len);
  emit_op(c, Op_Nil);
  push_depth(c);
  return true;
}

static bool emit_short_circuit(Compiler* c, Ref args, bool isAnd, bool tail) {
  State* vm = c->vm;
  if (ot_tag(vm, args) != Tag_Pair) {
    emit_op(c, isAnd ? Op_True : Op_False);
    push_depth(c);
    return true;
  }
  OT_SCOPE(vm);
  Ref cursor = ot_push_copy(vm, args);
  Ref item = ot_push(vm);
  Ref rest = ot_push(vm);
  VecU32 exits = {0};
  ot_cdr(vm, rest, cursor);
  while (ot_tag(vm, rest) == Tag_Pair) {
    ot_car(vm, item, cursor);
    if (!emit_expr(c, item, false)) {
      vec_deinit(&exits);
      return false;
    }
    vec_push(&exits, emit_jump(c, isAnd ? Op_JumpFalsePeek : Op_JumpTruePeek));
    emit_op(c, Op_Pop);
    pop_depth(c, 1);
    ot_copy(vm, cursor, rest);
    ot_cdr(vm, rest, cursor);
  }
  ot_car(vm, item, cursor);
  bool falls = emit_expr(c, item, tail);
  u32 end = c->bytes.len;
  for (u32 i = 0; i < exits.len; i++) patch_jump(c, exits.data[i], end);
  bool result = exits.len ? true : falls;
  vec_deinit(&exits);
  return result;
}

static bool emit_cond(Compiler* c, Ref clauses, bool tail) {
  State* vm = c->vm;
  OT_SCOPE(vm);
  Ref cursor = ot_push_copy(vm, clauses);
  VecU32 exits = {0};
  const u32 baseDepth = c->depth;
  bool anyFalls = false;
  bool hasElse = false;
  Ref clauseRoot = ot_push(vm);
  Ref part = ot_push(vm);
  Ref rest = ot_push(vm);

  while (ot_tag(vm, cursor) == Tag_Pair) {
    ot_car(vm, clauseRoot, cursor);
    if (ot_tag(vm, clauseRoot) != Tag_Pair) {
      compiler_error(c, "bad cond clause");
      vec_deinit(&exits);
      return true;
    }
    ot_car(vm, part, clauseRoot);
    ot_cdr(vm, rest, clauseRoot);
    if (symbol_is(vm, part, ot_syms(vm)->else_)) {
      if (ot_tag(vm, rest) != Tag_Pair) {
        compiler_error(c, "cond else needs a body");
        vec_deinit(&exits);
        return true;
      }
      hasElse = true;
      ot_copy(vm, part, rest);
      bool falls = emit_body(c, part, tail);
      anyFalls = anyFalls || falls;
      break;
    }

    if (!emit_expr(c, part, false)) {
      vec_deinit(&exits);
      return false;
    }
    if (ot_tag(vm, rest) != Tag_Pair) {
      // The clause's own test value is the result, so this exit reaches the end
      // of the cond carrying a value: the form falls through even if every
      // clause body below ends in a tail call.
      u32 next = emit_jump(c, Op_JumpFalsePeek);
      vec_push(&exits, emit_jump(c, Op_Jump));
      anyFalls = true;
      patch_jump(c, next, c->bytes.len);
      emit_op(c, Op_Pop);
      pop_depth(c, 1);
    } else {
      u32 next = emit_jump(c, Op_JumpFalse);
      pop_depth(c, 1);
      ot_copy(vm, part, rest);
      bool falls = emit_body(c, part, tail);
      if (falls) {
        vec_push(&exits, emit_jump(c, Op_Jump));
        anyFalls = true;
      }
      patch_jump(c, next, c->bytes.len);
    }
    c->depth = baseDepth;
    ot_cdr(vm, cursor, cursor);
  }

  if (!hasElse) {
    emit_op(c, Op_Nil);
    push_depth(c);
    anyFalls = true;
  }
  u32 end = c->bytes.len;
  for (u32 i = 0; i < exits.len; i++) patch_jump(c, exits.data[i], end);
  c->depth = anyFalls ? baseDepth + 1 : baseDepth;
  vec_deinit(&exits);
  return anyFalls;
}

static void emit_quoted_symbol(Compiler* c, u32 name) {
  emit_op(c, Op_Const);
  emit_u16(c, add_constant_imm(c, symbol_v(name)));
  push_depth(c);
}

static bool emit_quasiquote(Compiler* c, Ref form, u32 depth) {
  State* vm = c->vm;
  OT_SCOPE(vm);
  if (ot_tag(vm, form) != Tag_Pair) {
    emit_op(c, Op_Const);
    emit_u16(c, add_constant(c, form));
    push_depth(c);
    return true;
  }

  const Syms* syms = ot_syms(vm);
  Ref head = ot_push(vm);
  Ref args = ot_push(vm);
  Ref part = ot_push(vm);
  Ref rest = ot_push(vm);
  ot_car(vm, head, form);
  ot_cdr(vm, args, form);
  if (symbol_is(vm, head, syms->unquote_)) {
    if (ot_tag(vm, args) != Tag_Pair) {
      compiler_error(c, "bad unquote");
      return true;
    }
    ot_car(vm, part, args);
    if (depth == 1) return emit_expr(c, part, false);
    emit_quoted_symbol(c, syms->unquote_);
    if (!emit_quasiquote(c, part, depth - 1)) return false;
    emit_op(c, Op_List);
    emit_u16(c, 2);
    pop_depth(c, 1);
    return true;
  }
  if (symbol_is(vm, head, syms->quasiquote_)) {
    if (ot_tag(vm, args) != Tag_Pair) {
      compiler_error(c, "bad nested quasiquote");
      return true;
    }
    ot_car(vm, part, args);
    emit_quoted_symbol(c, syms->quasiquote_);
    if (!emit_quasiquote(c, part, depth + 1)) return false;
    emit_op(c, Op_List);
    emit_u16(c, 2);
    pop_depth(c, 1);
    return true;
  }

  bool splice = false;
  if (depth == 1 && ot_tag(vm, head) == Tag_Pair) {
    ot_car(vm, part, head);
    splice = symbol_is(vm, part, syms->unquoteSplicing_);
  }
  if (splice) {
    ot_cdr(vm, args, head);
    if (ot_tag(vm, args) != Tag_Pair) {
      compiler_error(c, "bad unquote-splicing");
      return true;
    }
    ot_car(vm, part, args);
    if (!emit_expr(c, part, false)) return false;
    ot_cdr(vm, rest, form);
    if (!emit_quasiquote(c, rest, depth)) return false;
    emit_op(c, Op_Append2);
    pop_depth(c, 1);
    return true;
  }

  if (!emit_quasiquote(c, head, depth)) return false;
  ot_cdr(vm, rest, form);
  if (!emit_quasiquote(c, rest, depth)) return false;
  emit_op(c, Op_Cons);
  pop_depth(c, 1);
  return true;
}

static void emit_constant(Compiler* c, Ref value) {
  emit_op(c, Op_Const);
  emit_u16(c, add_constant(c, value));
  push_depth(c);
}

static void emit_constant_imm(Compiler* c, Value immediate) {
  emit_op(c, Op_Const);
  emit_u16(c, add_constant_imm(c, immediate));
  push_depth(c);
}

static void emit_native(Compiler* c, const char* name, NativeFn native) {
  State* vm = c->vm;
  OT_SCOPE(vm);
  Ref function = ot_push(vm);
  ot_make_native(vm, function, name, native);
  emit_constant(c, function);
}

static bool emit_thunk_expr(Compiler* c, Ref form) {
  State* vm = c->vm;
  OT_SCOPE(vm);
  Ref body = ot_push(vm);
  Ref empty = ot_push(vm);
  ot_set_null(vm, empty);
  ot_cons(vm, body, form, empty);
  return emit_lambda(c, body, 0);
}

static bool emit_thunk_body(Compiler* c, Ref forms) { return emit_lambda(c, forms, 0); }

static bool emit_binding_control(Compiler* c, Ref args, bool tail, const char* badForm,
                                 const char* badBinding, const char* nativeName, NativeFn native,
                                 bool thunkBindings) {
  State* vm = c->vm;
  if (ot_tag(vm, args) != Tag_Pair) {
    compiler_error(c, badForm);
    return true;
  }
  OT_SCOPE(vm);
  emit_native(c, nativeName, native);
  Ref bindings = ot_push(vm);
  Ref binding = ot_push(vm);
  Ref part = ot_push(vm);
  Ref rest = ot_push(vm);
  ot_car(vm, bindings, args);
  strip_array_head(vm, bindings, bindings);
  u32 argc = 0;
  while (ot_tag(vm, bindings) == Tag_Pair) {
    ot_car(vm, binding, bindings);
    if (ot_tag(vm, binding) != Tag_Pair) {
      compiler_error(c, badBinding);
      return true;
    }
    ot_car(vm, part, binding);
    ot_cdr(vm, rest, binding);
    if (ot_tag(vm, rest) != Tag_Pair) {
      compiler_error(c, badBinding);
      return true;
    }
    if (thunkBindings ? !emit_thunk_expr(c, part) : !emit_expr(c, part, false)) return false;
    ot_car(vm, part, rest);
    if (thunkBindings ? !emit_thunk_expr(c, part) : !emit_expr(c, part, false)) return false;
    argc += 2;
    ot_cdr(vm, bindings, bindings);
  }
  ot_cdr(vm, part, args);
  if (!emit_thunk_body(c, part)) return false;
  return finish_control_call(c, argc + 1, tail);
}

static bool emit_handler_bind(Compiler* c, Ref args, bool tail) {
  return emit_binding_control(c, args, tail, "bad handler-bind", "bad handler-bind binding",
                              "%handler-bind", vm_control_handler_bind, false);
}

static bool emit_restart_case(Compiler* c, Ref args, bool tail) {
  State* vm = c->vm;
  if (ot_tag(vm, args) != Tag_Pair) {
    compiler_error(c, "bad restart-case");
    return true;
  }
  OT_SCOPE(vm);
  emit_native(c, "%restart-case", vm_control_restart_case);
  Ref part = ot_push(vm);
  Ref clauses = ot_push(vm);
  Ref clause = ot_push(vm);
  Ref name = ot_push(vm);
  Ref rest = ot_push(vm);
  Ref doc = ot_push(vm);
  ot_car(vm, part, args);
  if (!emit_thunk_expr(c, part)) return false;
  u32 argc = 1;
  ot_cdr(vm, clauses, args);
  while (ot_tag(vm, clauses) == Tag_Pair) {
    ot_car(vm, clause, clauses);
    if (ot_tag(vm, clause) != Tag_Pair) {
      compiler_error(c, "bad restart-case clause");
      return true;
    }
    ot_car(vm, name, clause);
    if (ot_tag(vm, name) != Tag_Symbol) {
      compiler_error(c, "bad restart-case clause");
      return true;
    }
    ot_cdr(vm, rest, clause);
    skip_docstring_ref(vm, rest, doc, rest);
    if (ot_tag(vm, rest) != Tag_Pair) {
      compiler_error(c, "restart-case clause needs parameters");
      return true;
    }
    u32 clauseName = ot_id(vm, name);
    emit_constant_imm(c, symbol_v(clauseName));
    emit_constant(c, doc);
    ot_cdr(vm, part, rest);
    if (!emit_lambda(c, part, clauseName)) return false;
    argc += 3;
    ot_cdr(vm, clauses, clauses);
  }
  return finish_control_call(c, argc, tail);
}

static bool emit_try(Compiler* c, Ref args, bool tail) {
  State* vm = c->vm;
  OT_SCOPE(vm);
  Ref cursor = ot_push_copy(vm, args);
  Ref scan = ot_push_copy(vm, args);
  Ref form = ot_push(vm);
  Ref head = ot_push(vm);
  u32 bodyCount = 0;
  while (ot_tag(vm, scan) == Tag_Pair) {
    ot_car(vm, form, scan);
    if (ot_tag(vm, form) == Tag_Pair) {
      ot_car(vm, head, form);
      if (symbol_is(vm, head, ot_syms(vm)->catch_)) break;
    }
    bodyCount++;
    ot_cdr(vm, scan, scan);
  }

  emit_native(c, "%try", vm_control_try);
  emit_constant_imm(c, int_v(bodyCount));
  u32 argc = 1;
  Ref part = ot_push(vm);
  for (u32 i = 0; i < bodyCount; i++) {
    ot_car(vm, part, cursor);
    if (!emit_thunk_expr(c, part)) return false;
    argc++;
    ot_cdr(vm, cursor, cursor);
  }
  Ref clause = ot_push(vm);
  Ref rest = ot_push(vm);
  Ref spec = ot_push(vm);
  Ref specRest = ot_push(vm);
  while (ot_tag(vm, cursor) == Tag_Pair) {
    ot_car(vm, clause, cursor);
    if (ot_tag(vm, clause) != Tag_Pair) {
      compiler_error(c, "bad catch clause");
      return true;
    }
    ot_car(vm, head, clause);
    ot_cdr(vm, rest, clause);
    if (!symbol_is(vm, head, ot_syms(vm)->catch_) || ot_tag(vm, rest) != Tag_Pair) {
      compiler_error(c, "bad catch clause");
      return true;
    }
    ot_car(vm, spec, rest);
    if (ot_tag(vm, spec) != Tag_Pair) {
      compiler_error(c, "bad catch specification");
      return true;
    }
    ot_cdr(vm, specRest, spec);
    if (ot_tag(vm, specRest) != Tag_Pair) {
      compiler_error(c, "bad catch specification");
      return true;
    }
    ot_car(vm, head, specRest);
    if (ot_tag(vm, head) != Tag_Symbol) {
      compiler_error(c, "bad catch specification");
      return true;
    }
    ot_car(vm, part, spec);
    if (!emit_thunk_expr(c, part)) return false;
    ot_cdr(vm, part, rest);
    if (!emit_lambda(c, part, 0)) return false;
    argc += 2;
    ot_cdr(vm, cursor, cursor);
  }
  return finish_control_call(c, argc, tail);
}

static bool emit_unwind_protect(Compiler* c, Ref args, bool tail) {
  State* vm = c->vm;
  if (ot_tag(vm, args) != Tag_Pair) {
    compiler_error(c, "bad unwind-protect");
    return true;
  }
  OT_SCOPE(vm);
  Ref cursor = ot_push_copy(vm, args);
  Ref part = ot_push(vm);
  emit_native(c, "%unwind-protect", vm_control_unwind_protect);
  u32 argc = 0;
  while (ot_tag(vm, cursor) == Tag_Pair) {
    ot_car(vm, part, cursor);
    if (!emit_thunk_expr(c, part)) return false;
    argc++;
    ot_cdr(vm, cursor, cursor);
  }
  return finish_control_call(c, argc, tail);
}

static bool emit_with_params(Compiler* c, Ref args, bool tail) {
  return emit_binding_control(c, args, tail, "bad with-params", "bad with-params binding",
                              "%with-params", vm_control_with_params, true);
}

static bool emit_defparam(Compiler* c, Ref args, bool tail) {
  State* vm = c->vm;
  if (c->info->userDepth > 0 || c->active.len > c->info->initialCount) {
    compiler_error(c, "defparam only allowed at top level");
    return true;
  }
  OT_SCOPE(vm);
  Ref name = ot_push(vm);
  Ref rest = ot_push(vm);
  Ref doc = ot_push(vm);
  if (ot_tag(vm, args) != Tag_Pair) {
    compiler_error(c, "bad defparam");
    return true;
  }
  ot_car(vm, name, args);
  if (ot_tag(vm, name) != Tag_Symbol) {
    compiler_error(c, "bad defparam");
    return true;
  }
  ot_cdr(vm, rest, args);
  skip_docstring_ref(vm, rest, doc, rest);
  if (ot_tag(vm, rest) != Tag_Pair) {
    compiler_error(c, "defparam missing default");
    return true;
  }
  emit_native(c, "%defparam", vm_control_defparam);
  emit_constant_imm(c, symbol_v(ot_id(vm, name)));
  emit_constant(c, doc);
  ot_car(vm, rest, rest);
  if (!emit_expr(c, rest, false)) return false;
  return finish_control_call(c, 3, tail);
}

static bool emit_data_control(Compiler* c, Ref args, bool tail, const char* helperName,
                              NativeFn native, bool requireArg) {
  State* vm = c->vm;
  OT_SCOPE(vm);
  Ref cursor = ot_push_copy(vm, args);
  if (requireArg && ot_tag(vm, cursor) != Tag_Pair) {
    compiler_error(c, "missing control form argument");
    return true;
  }
  emit_native(c, helperName, native);
  u32 argc = 0;
  Ref part = ot_push(vm);
  while (ot_tag(vm, cursor) == Tag_Pair) {
    ot_car(vm, part, cursor);
    emit_constant(c, part);
    argc++;
    ot_cdr(vm, cursor, cursor);
  }
  return finish_control_call(c, argc, tail);
}

static bool emit_expr(Compiler* c, Ref form, bool tail) {
  State* vm = c->vm;
  OT_SCOPE(vm);
  Tag tag = ot_tag(vm, form);
  if (tag == Tag_Symbol) {
    emit_load(c, resolve(c, ot_id(vm, form)));
    return true;
  }
  if (tag != Tag_Pair) {
    switch (tag) {
      case Tag_Nil: emit_op(c, Op_Nil); break;
      case Tag_True: emit_op(c, Op_True); break;
      case Tag_False: emit_op(c, Op_False); break;
      case Tag_Null: emit_op(c, Op_Null); break;
      case Tag_Int: {
        i64 n = ot_int(vm, form);
        if (n >= INT8_MIN && n <= INT8_MAX) {
          emit_op(c, Op_Int8);
          vec_push(&c->bytes, (char)(i8)n);
        } else {
          emit_op(c, Op_Const);
          emit_u16(c, add_constant(c, form));
        }
        break;
      }
      default:
        emit_op(c, Op_Const);
        emit_u16(c, add_constant(c, form));
        break;
    }
    push_depth(c);
    return true;
  }

  const Syms* syms = ot_syms(vm);
  Ref head = ot_push(vm);
  Ref args = ot_push(vm);
  Ref part = ot_push(vm);
  Ref rest = ot_push(vm);
  ot_car(vm, head, form);
  ot_cdr(vm, args, form);
  if (ot_tag(vm, head) == Tag_Symbol) {
    u32 name = ot_id(vm, head);
    if (name == syms->quote_) {
      if (ot_tag(vm, args) != Tag_Pair) {
        compiler_error(c, "bad quote");
        ot_set_nil(vm, part);
      } else {
        ot_car(vm, part, args);
      }
      emit_op(c, Op_Const);
      emit_u16(c, add_constant(c, part));
      push_depth(c);
      return true;
    }
    if (name == syms->quasiquote_) {
      if (ot_tag(vm, args) != Tag_Pair) {
        compiler_error(c, "bad quasiquote");
        return true;
      }
      ot_car(vm, part, args);
      return emit_quasiquote(c, part, 1);
    }
    if (name == syms->unquote_ || name == syms->unquoteSplicing_) {
      compiler_error(c, "unquote outside quasiquote");
      return true;
    }
    if (name == syms->if_) return emit_if(c, args, tail);
    if (name == syms->begin_ || name == syms->do_) return emit_body(c, args, tail);
    if (name == syms->lambda_ || name == syms->fn_) {
      if (ot_tag(vm, args) != Tag_Pair) {
        compiler_error(c, "bad lambda");
        return true;
      }
      ot_cdr(vm, part, args);
      return emit_lambda(c, part, 0);
    }
    if (name == syms->let_) return emit_let(c, args, tail);
    if (is_define_head(vm, name)) return emit_define(c, form, name == syms->definePriv_);
    if (name == syms->defmacro_) return emit_defmacro(c, form);
    if (name == syms->setBang_) {
      if (ot_tag(vm, args) != Tag_Pair) {
        compiler_error(c, "bad set!");
        return true;
      }
      ot_car(vm, part, args);
      ot_cdr(vm, rest, args);
      if (ot_tag(vm, part) != Tag_Symbol || ot_tag(vm, rest) != Tag_Pair) {
        compiler_error(c, "bad set!");
        return true;
      }
      u32 target = ot_id(vm, part);
      ot_car(vm, part, rest);
      if (!emit_expr(c, part, false)) return false;
      emit_store(c, resolve(c, target));
      return true;
    }
    if (name == syms->while_) return emit_while(c, args);
    if (name == syms->and_) return emit_short_circuit(c, args, true, tail);
    if (name == syms->or_) return emit_short_circuit(c, args, false, tail);
    if (name == syms->cond_) return emit_cond(c, args, tail);
    if (name == syms->handlerBind_) return emit_handler_bind(c, args, tail);
    if (name == syms->restartCase_) return emit_restart_case(c, args, tail);
    if (name == syms->try_) return emit_try(c, args, tail);
    if (name == syms->unwindProtect_ || name == syms->defer_)
      return emit_unwind_protect(c, args, tail);
    if (name == syms->withParams_) return emit_with_params(c, args, tail);
    if (name == syms->defparam_) return emit_defparam(c, args, tail);
    if (name == syms->ns_) return emit_data_control(c, args, tail, "%ns", vm_control_ns, true);
    if (name == syms->inNs_)
      return emit_data_control(c, args, tail, "%in-ns", vm_control_in_ns, true);
    if (name == syms->require_)
      return emit_data_control(c, args, tail, "%require", vm_control_require, false);
  }
  return emit_call(c, form, tail);
}

static Value compile_lambda(State* vm, LambdaInfo* info, Ref dst, Ref body, u32 name) {
  OT_SCOPE(vm);
  Ref bodyRoot = ot_push(vm);
  Ref doc = ot_push(vm);
  skip_docstring_ref(vm, bodyRoot, doc, body);
  Ref constants = ot_push(vm);
  ot_make_array(vm, constants, 8);
  Compiler compiler;
  compiler_init(&compiler, vm, info, constants);

  for (u32 i = 0; i < info->initialCount; i++) {
    Binding* binding = &info->bindings.data[i];
    if (!binding->captured) continue;
    emit_op(&compiler, Op_GetLocal);
    emit_u16(&compiler, binding->slot);
    push_depth(&compiler);
    emit_op(&compiler, Op_MakeBox);
    emit_op(&compiler, Op_SetLocal);
    emit_u16(&compiler, binding->slot);
    emit_op(&compiler, Op_Pop);
    pop_depth(&compiler, 1);
  }

  bool falls = emit_body(&compiler, bodyRoot, true);
  if (falls) emit_op(&compiler, Op_Return);
  if (compiler.failed) {
    compiler_deinit(&compiler);
    return unwind_v();
  }
  CodeSpec spec = {
      .nfixed = info->nfixed,
      .hasRest = info->hasRest,
      .nupvals = info->captures.len,
      .nlocals = info->bindings.len,
      .maxStack = compiler.maxDepth,
      .name = name,
  };
  Value status = make_code_ref(vm, dst, (const u8*)compiler.bytes.data, compiler.bytes.len,
                               constants, &spec);
  compiler_deinit(&compiler);
  return status;
}

Value compile_form_ref(State* vm, Ref dst, Ref expanded) {
  OT_SCOPE(vm);
  Ref body = ot_push(vm);
  Ref empty = ot_push(vm);
  ot_set_null(vm, empty);
  ot_cons(vm, body, expanded, empty);
  LambdaInfo top;
  lambda_info_init(&top, nullptr, 0);
  analyze_body(vm, &top, body);
  Value status = compile_lambda(vm, &top, dst, body, 0);
  lambda_info_deinit(&top);
  return status;
}
