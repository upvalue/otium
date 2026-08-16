// Otium CLI + REPL. Host-facing standard-library and OS integration lives
// here rather than in the low-memory runtime.

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <ctype.h>
#include <errno.h>
#include <limits.h>

#include "bestline.h"
#include "common.h"
#include "vec.h"
#include "value.h"
#include "heap.h"
#include "state.h"
#include "printer.h"
#include "eval.h"
#include "ns.h"

#ifdef OT_EXT_DEMO
#include "../ext/demo/demo_ext.h"
#endif
#ifdef OT_EXT_RAYLIB
#include "../ext/raylib/raylib_ext.h"
#endif

// state_push_handler/state_pop_handler come from state.h; make_native from eval.h.

// ---------------------------------------------------------------------------
// globals

static State* g_vm = nullptr;
static volatile sig_atomic_t g_sigint = 0;
static bool g_replMode = false;
static bool g_quitRequested = false;

OT_VEC_TYPE(char*, VecStr);
static VecStr g_loadPath = {0};

static void on_sigint(int sig) {
  (void)sig;
  g_sigint = 1;
  if (g_vm) g_vm->interruptFlag = true;  // plain store, as specified
  if (!g_replMode) {
    // Script mode: if the interpreter never re-checks (e.g. blocked), a second
    // ^C from the user will kill us anyway; the eval loop exits 130 below.
  }
}

// ---------------------------------------------------------------------------
// host callbacks

static void host_write(void* ud, const char* s, u32 n) {
  (void)ud;
  fwrite(s, 1, n, stdout);
}

// require callback: ns name dots->slashes + ".scm", searched across load path.
static bool host_load(void* ud, const char* nsName, Buf* srcOut) {
  (void)ud;
  Buf rel = {0};
  buf_append_cstr(&rel, nsName);
  for (u32 i = 0; i < rel.len; i++)
    if (rel.data[i] == '.') rel.data[i] = '/';
  buf_append_cstr(&rel, ".scm");
  for (u32 d = 0; d < g_loadPath.len; d++) {
    const char* dir = g_loadPath.data[d];
    Buf path = {0};
    if (dir[0] != '\0') {
      buf_append_cstr(&path, dir);
      buf_append_cstr(&path, "/");
    }
    buf_append(&path, rel.data, rel.len);
    buf_append(&path, "\0", 1);  // NUL-terminate for fopen
    FILE* f = fopen(path.data, "rb");
    buf_deinit(&path);
    if (!f) continue;
    char chunk[4096];
    size_t n;
    while ((n = fread(chunk, 1, sizeof chunk, f)) > 0) buf_append(srcOut, chunk, (u32)n);
    fclose(f);
    buf_deinit(&rel);
    return true;
  }
  buf_deinit(&rel);
  return false;
}

// ---------------------------------------------------------------------------
// small helpers

static Value resolve_named(State* vm, const char* name) {
  u32 id = intern_id(&vm->intern, name, (u32)strlen(name));
  return ns_resolve(vm, symbol_v(id));
}

// call an otium function by name with argc args already pushed at `base`
static Value call_named(State* vm, const char* name, u32 base, u32 argc) {
  Value fn = resolve_named(vm, name);
  if (fn.tag == Tag_Unwind) return fn;
  return apply(vm, fn, base, argc);
}

static void print_value(State* vm, Value v, FILE* to) {
  Buf out = {0};
  print_repr(vm, v, &out);
  fwrite(out.data, 1, out.len, to);
  fputc('\n', to);
  buf_deinit(&out);
}

typedef enum QuitReason : u8 {
  QuitReason_None,
  QuitReason_Interrupt,
  QuitReason_Requested,
} QuitReason;

static QuitReason take_quit(State* vm) {
  if (vm->unwindKind != UnwindKind_Quit) return QuitReason_None;
  if (g_sigint) {
    g_sigint = 0;
    return QuitReason_Interrupt;
  }
  g_quitRequested = true;
  return QuitReason_Requested;
}

// ---------------------------------------------------------------------------
// flagship: native handler that offers restarts interactively

static Value repl_condition_handler(State* vm, u32 base, u32 argc) {
  Value cond = argc > 0 ? vm->stack.data[base] : nil_v();

  fputs("\nUnhandled condition: ", stderr);
  print_value(vm, cond, stderr);

  // compute-restarts -> array, innermost first (spec 8.5)
  u32 b = vm->stack.len;
  Value restarts = call_named(vm, "compute-restarts", b, 0);
  if (restarts.tag == Tag_Unwind) return restarts;
  u32 sc = scope_begin(vm);
  Slot rS = scope_push(vm, restarts);  // root across allocations below

  // list them
  i64 count = 0;
  for (;; ++count) {
    Value name, desc;
    {
      u32 s2 = scope_begin(vm);
      Slot r = scope_push(vm, array_get(slot_get(rS), count));
      if (is_nil(slot_get(r))) {
        scope_pop_to(vm, s2);
        break;
      }
      u32 ab = vm->stack.len;
      state_push(vm, slot_get(r));
      name = call_named(vm, "restart-name", ab, 1);
      state_pop_to(vm, ab);
      state_push(vm, slot_get(r));
      desc = call_named(vm, "restart-description", ab, 1);
      scope_pop_to(vm, s2);
    }
    // name is a symbol (immediate); desc is used with no allocation between
    // the call above and the prints below (Buf/print_display are C-heap only)
    Buf line = {0};
    buf_printf(&line, "  [%lld] ", (long long)count);
    if (name.tag != Tag_Unwind) print_display(vm, name, &line);
    if (desc.tag != Tag_Unwind && !is_nil(desc)) {
      buf_append_cstr(&line, " — ");
      print_display(vm, desc, &line);
    }
    fwrite(line.data, 1, line.len, stderr);
    fputc('\n', stderr);
    buf_deinit(&line);
  }

  if (count == 0) {
    fputs("(no active restarts)\n", stderr);
    return scope_exit(vm, sc, nil_v());  // decline; condition unwinds to the REPL loop
  }

  for (;;) {
    fputs("restart #? (or press enter to unwind) ", stderr);
    fflush(stderr);
    char buf[128];
    if (!fgets(buf, sizeof buf, stdin)) return scope_exit(vm, sc, nil_v());
    if (buf[0] == '\n') return scope_exit(vm, sc, nil_v());  // decline
    char* end = nullptr;
    long idx = strtol(buf, &end, 10);
    if (end == buf || idx < 0 || idx >= count) {
      fputs("invalid choice\n", stderr);
      continue;
    }
    u32 ib = vm->stack.len;
    state_push(vm, array_get(slot_get(rS), idx));
    // invoke-restart with a restart value unwinds to exactly that restart;
    // the resulting Unwind propagates out of this handler and resumes there.
    return scope_exit(vm, sc, call_named(vm, "invoke-restart", ib, 1));
  }
}

static Value always_true_pred(State* vm, u32 base, u32 argc) {
  (void)vm;
  (void)base;
  (void)argc;
  return bool_v(true);
}

// ---------------------------------------------------------------------------
// file runner

static int run_file(State* vm, const char* path) {
  FILE* f = fopen(path, "rb");
  if (!f) {
    fprintf(stderr, "otium: cannot open %s\n", path);
    return 1;
  }
  Buf src = {0};
  char chunk[4096];
  size_t n;
  while ((n = fread(chunk, 1, sizeof chunk, f)) > 0) buf_append(&src, chunk, (u32)n);
  fclose(f);

  Value result = eval_source(vm, src.data, src.len, path);
  buf_deinit(&src);
  if (result.tag == Tag_Unwind) {
    QuitReason reason = take_quit(vm);
    if (reason != QuitReason_None) {
      state_cancel_unwind(vm);
      return reason == QuitReason_Interrupt ? 130 : 0;
    }
    print_value(vm, vm->unwindCondition, stderr);
    return 1;
  }
  return 0;
}

// ---------------------------------------------------------------------------
// REPL

typedef struct ReplEvalContext {
  Slot pred;
  Slot handler;
} ReplEvalContext;

static Value eval_repl_form(State* vm, Value form, void* data) {
  ReplEvalContext* context = (ReplEvalContext*)data;
  Value pushed = state_push_handler(vm, slot_get(context->pred), slot_get(context->handler));
  (void)pushed;
  Value result = eval_form(vm, form);
  state_pop_handler(vm);
  return result;
}

static void print_repl_result(State* vm, Value result, u32 consumed, void* data) {
  (void)consumed;
  (void)data;
  // A REPL reports every evaluation result, including nil.
  print_value(vm, result, stdout);
}

// The server protocol uses one control character per direction. A request is
// source text followed by a line containing US; each response ends with RS
// and "ot> ". Source can contain any number of lines and top-level forms.
#define SERVER_REQUEST_END '\x1f'
#define SERVER_RESPONSE_END "\x1eot> "

static bool read_server_line(Buf* line) {
  buf_clear(line);
  for (;;) {
    int c = fgetc(stdin);
    if (c == EOF) return line->len != 0;
    if (c == '\n') return true;
    char ch = (char)c;
    buf_append(line, &ch, 1);
  }
}

static Value eval_server_form(State* vm, Value form, void* data) {
  (void)data;
  return eval_form(vm, form);
}

static void finish_server_response(void) {
  fputs(SERVER_RESPONSE_END, stdout);
  fflush(stdout);
}

static void run_server(State* vm) {
  g_replMode = true;
  Buf source = {0};
  Buf line = {0};

  while (read_server_line(&line)) {
    if (!(line.len == 1 && line.data[0] == SERVER_REQUEST_END)) {
      if (source.len != 0) buf_append(&source, "\n", 1);
      buf_append(&source, line.data, line.len);
      continue;
    }

    bool stop = false;
    if (source.len != 0) {
      EvalSourceState sourceState = {0};
      EvalSourcePolicy policy = {nullptr, eval_server_form, print_repl_result, &sourceState};
      Value result = eval_source_policy(vm, source.data, source.len, "<server>", &policy);
      if (result.tag == Tag_Unwind) {
        QuitReason reason = take_quit(vm);
        if (reason == QuitReason_Interrupt) {
          vm->interruptFlag = false;
          puts("interrupted");
        } else if (reason == QuitReason_Requested) {
          stop = true;
        } else {
          fputs("error: ", stdout);
          print_value(vm, vm->unwindCondition, stdout);
        }
        state_cancel_unwind(vm);
      }
    }

    buf_clear(&source);
    if (stop) break;
    finish_server_response();
  }
  buf_deinit(&source);
  buf_deinit(&line);
}

static void run_repl(State* vm) {
  g_replMode = true;
  printf("otium repl — ^C interrupts, ^D exits\n");
  // Both natives stay rooted in these slots for the whole session; read them
  // through the slots at each install — a raw copy would go stale as soon as
  // an eval allocates.
  Slot handlerFn = {
      vm, state_push(vm, make_native(vm, "repl-condition-handler", repl_condition_handler))};
  Slot predFn = {vm, state_push(vm, nil_v())};
  slot_set(predFn, make_native(vm, "repl-any-pred", always_true_pred));

  Buf pending = {0};
  for (;;) {
    const char* prompt = pending.len == 0 ? "ot> " : "..> ";
    char* line = bestlineWithHistory(prompt, "otium");
    if (!line) {
      if (g_sigint) {
        g_sigint = 0;
        vm->interruptFlag = false;
        buf_clear(&pending);
        continue;
      }
      break;  // EOF
    }
    // skip blank lines
    bool blank = true;
    size_t len = strlen(line);
    for (size_t i = 0; i < len; ++i)
      if (!isspace((unsigned char)line[i])) {
        blank = false;
        break;
      }
    if (blank && pending.len == 0) {
      bestlineFree(line);
      continue;
    }

    if (pending.len != 0) buf_append(&pending, "\n", 1);
    buf_append(&pending, line, (u32)len);
    bestlineFree(line);

    bool needMore = false;
    bool stop = false;
    EvalSourceState sourceState = {0};
    ReplEvalContext context = {predFn, handlerFn};
    EvalSourcePolicy policy = {&context, eval_repl_form, print_repl_result, &sourceState};
    Value result = eval_source_policy(vm, pending.data, pending.len, "<repl>", &policy);
    u32 consumed = sourceState.consumed;
    if (result.tag == Tag_Unwind) {
      if (sourceState.readError) {
        if (sourceState.incomplete) {
          state_cancel_unwind(vm);
          needMore = true;
        } else {
          print_value(vm, vm->unwindCondition, stderr);
          state_cancel_unwind(vm);
          consumed = pending.len;
        }
      } else {
        QuitReason reason = take_quit(vm);
        if (reason == QuitReason_Interrupt) {
          vm->interruptFlag = false;
          puts("interrupted");
        } else if (reason == QuitReason_None) {
          fputs("error: ", stderr);
          print_value(vm, vm->unwindCondition, stderr);
        } else {
          stop = true;
        }
        state_cancel_unwind(vm);
        consumed = pending.len;
      }
    }
    if (consumed) {
      memmove(pending.data, pending.data + consumed, pending.len - consumed);
      pending.len -= consumed;
    }
    if (stop) break;
    if (needMore) continue;
  }
  buf_deinit(&pending);
  bestlineHistoryFree();
  putchar('\n');
}

// ---------------------------------------------------------------------------
// main

typedef struct CliOptions {
  const char* script;
  bool repl;
  bool server;
  u32 maxDepth;
  u32 stackSlots;
  u32 heapInit;
  u32 heapMax;
} CliOptions;

static void print_usage(FILE* to) {
  fputs("Usage: otium [OPTIONS] [FILE]\n"
        "Run FILE, or start a REPL when no FILE is given.\n"
        "\n"
        "Options:\n"
        "  --repl             Start a REPL after loading FILE\n"
        "  --server           Run the framed stdio evaluation server\n"
        "  --path DIR         Add DIR to the module search path (repeatable)\n"
        "  --max-depth N      Maximum evaluator recursion depth\n"
        "  --stack-slots N    Maximum live value-stack slots\n"
        "  --heap-init BYTES  Initial semispace size\n"
        "  --heap-max BYTES   Maximum semispace size\n"
        "  -h, --help         Show this help\n",
        to);
}

static bool parse_u32_arg(const char* option, const char* text, u32* out) {
  errno = 0;
  char* end = nullptr;
  unsigned long long n = strtoull(text, &end, 10);
  if (errno || end == text || *end != '\0' || n == 0 || n > UINT32_MAX) {
    fprintf(stderr, "otium: %s requires an integer from 1 to %u\n", option, UINT32_MAX);
    return false;
  }
  *out = (u32)n;
  return true;
}

static char* dup_cstr(const char* s, size_t n) {
  char* copy = malloc(n + 1);
  if (!copy) ot_fatal("out of memory");
  memcpy(copy, s, n);
  copy[n] = '\0';
  return copy;
}

static int parse_args(int argc, char** argv, CliOptions* options) {
  bool positionalOnly = false;
  for (int i = 1; i < argc; ++i) {
    const char* arg = argv[i];
    if (!positionalOnly && strcmp(arg, "--") == 0) {
      positionalOnly = true;
    } else if (!positionalOnly && strcmp(arg, "--repl") == 0) {
      options->repl = true;
    } else if (!positionalOnly && strcmp(arg, "--server") == 0) {
      options->server = true;
    } else if (!positionalOnly && strcmp(arg, "--path") == 0) {
      if (++i == argc) {
        fputs("otium: --path requires a directory\n", stderr);
        return 2;
      }
      vec_push(&g_loadPath, dup_cstr(argv[i], strlen(argv[i])));
    } else if (!positionalOnly &&
               (strcmp(arg, "--max-depth") == 0 || strcmp(arg, "--stack-slots") == 0 ||
                strcmp(arg, "--heap-init") == 0 || strcmp(arg, "--heap-max") == 0)) {
      if (++i == argc) {
        fprintf(stderr, "otium: %s requires a value\n", arg);
        return 2;
      }
      u32* target = strcmp(arg, "--max-depth") == 0     ? &options->maxDepth
                    : strcmp(arg, "--stack-slots") == 0 ? &options->stackSlots
                    : strcmp(arg, "--heap-init") == 0   ? &options->heapInit
                                                        : &options->heapMax;
      if (!parse_u32_arg(arg, argv[i], target)) return 2;
    } else if (!positionalOnly && (strcmp(arg, "--help") == 0 || strcmp(arg, "-h") == 0)) {
      print_usage(stdout);
      return 1;
    } else if (!positionalOnly && arg[0] == '-' && arg[1] != '\0') {
      fprintf(stderr, "otium: unknown option %s\n", arg);
      fputs("Try 'otium --help' for more information.\n", stderr);
      return 2;
    } else if (!options->script) {
      options->script = arg;
    } else {
      fprintf(stderr, "otium: unexpected argument %s\n", arg);
      return 2;
    }
  }
  if (options->repl && options->server) {
    fputs("otium: --repl and --server cannot be used together\n", stderr);
    return 2;
  }
  if (options->heapInit < 1024) {
    fputs("otium: --heap-init must be at least 1024 bytes\n", stderr);
    return 2;
  }
  if (options->heapMax < options->heapInit) {
    fputs("otium: --heap-max must be at least --heap-init\n", stderr);
    return 2;
  }
  return 0;
}

int main(int argc, char** argv) {
  CliOptions options = {
      .script = nullptr,
      .repl = false,
      .server = false,
      .maxDepth = 512,
      .stackSlots = 4096,
      .heapInit = 4u * 1024 * 1024,
      .heapMax = 64u * 1024 * 1024,
  };
  int parseStatus = parse_args(argc, argv, &options);
  if (parseStatus != 0) return parseStatus == 1 ? 0 : parseStatus;

  const char* env = getenv("OTIUM_PATH");
  if (env) {
    const char* start = env;
    for (;;) {
      const char* colon = strchr(start, ':');
      size_t n = colon ? (size_t)(colon - start) : strlen(start);
      if (n) vec_push(&g_loadPath, dup_cstr(start, n));
      if (!colon) break;
      start = colon + 1;
    }
  }
  if (g_loadPath.len == 0) vec_push(&g_loadPath, dup_cstr(".", 1));

  StateConfig cfg = state_config_default();
  cfg.heapBytes = options.heapInit;
  cfg.stackSlots = options.stackSlots;
  cfg.maxDepth = options.maxDepth;
  cfg.heapMaxBytes = options.heapMax;
  State* vm = state_create(&cfg);
  if (!vm) {
    fputs("otium: vm creation failed\n", stderr);
    return 1;
  }
  g_vm = vm;
  vm->writeFn = host_write;
  vm->writeUd = nullptr;
  vm->loadFn = host_load;
  vm->loadUd = nullptr;

#ifdef OT_EXT_DEMO
  register_demo_extension(vm);
#endif
#ifdef OT_EXT_RAYLIB
  register_raylib_extension(vm);
#endif

  struct sigaction sa;
  memset(&sa, 0, sizeof sa);
  sa.sa_handler = on_sigint;
  sigaction(SIGINT, &sa, nullptr);

  int status = 0;
  if (options.script) status = run_file(vm, options.script);
  if (status == 0 && !g_quitRequested && options.server) {
    run_server(vm);
  } else if (status == 0 && !g_quitRequested && (options.repl || !options.script)) {
    run_repl(vm);
  }

  state_destroy(vm);
  g_vm = nullptr;
  return status;
}
