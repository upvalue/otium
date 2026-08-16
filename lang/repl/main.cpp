// Otium CLI + REPL. Host-facing standard-library and OS integration lives
// here rather than in the low-memory runtime.

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <csignal>
#include <cctype>
#include <cerrno>
#include <climits>
#include <string>
#include <vector>

#include "bestline.h"
#include "common.hpp"
#include "vec.hpp"
#include "value.hpp"
#include "heap.hpp"
#include "state.hpp"
#include "printer.hpp"
#include "eval.hpp"
#include "ns.hpp"

#ifdef OT_EXT_DEMO
#include "../ext/demo/demo_ext.hpp"
#endif
#ifdef OT_EXT_RAYLIB
#include "../ext/raylib/raylib_ext.hpp"
#endif

// state_push_handler/state_pop_handler come from state.hpp; make_native from eval.hpp.

using ot::Buf;
using ot::Tag;
using ot::u32;
using ot::Value;
using ot::State;

// ---------------------------------------------------------------------------
// globals

static State* g_vm = nullptr;
static volatile sig_atomic_t g_sigint = 0;
static bool g_replMode = false;
static bool g_quitRequested = false;
static std::vector<std::string> g_loadPath;

static void on_sigint(int) {
  g_sigint = 1;
  if (g_vm) g_vm->interruptFlag = true;  // plain store, as specified
  if (!g_replMode) {
    // Script mode: if the interpreter never re-checks (e.g. blocked), a second
    // ^C from the user will kill us anyway; the eval loop exits 130 below.
  }
}

// ---------------------------------------------------------------------------
// host callbacks

static void host_write(void*, const char* s, u32 n) { fwrite(s, 1, n, stdout); }

// require callback: ns name dots->slashes + ".scm", searched across load path.
static bool host_load(void*, const char* nsName, Buf* srcOut) {
  std::string rel(nsName);
  for (auto& c : rel)
    if (c == '.') c = '/';
  rel += ".scm";
  for (const auto& dir : g_loadPath) {
    std::string path = dir.empty() ? rel : dir + "/" + rel;
    FILE* f = fopen(path.c_str(), "rb");
    if (!f) continue;
    char chunk[4096];
    size_t n;
    while ((n = fread(chunk, 1, sizeof chunk, f)) > 0) srcOut->append(chunk, (u32)n);
    fclose(f);
    return true;
  }
  return false;
}

// ---------------------------------------------------------------------------
// small helpers

static Value resolve_named(State& vm, const char* name) {
  u32 id = vm.intern.intern(name, (u32)strlen(name));
  return ot::ns_resolve(vm, ot::symbol_v(id));
}

// call an otium function by name with argc args already pushed at `base`
static Value call_named(State& vm, const char* name, u32 base, u32 argc) {
  Value fn = resolve_named(vm, name);
  if (fn.tag == Tag::Unwind) return fn;
  return ot::apply(vm, fn, base, argc);
}

static void print_value(State& vm, Value v, FILE* to) {
  Buf out;
  ot::print_repr(vm, v, out);
  fwrite(out.data, 1, out.len, to);
  fputc('\n', to);
}

enum class QuitReason { None, Interrupt, Requested };

static QuitReason take_quit(State& vm) {
  if (vm.unwindKind != ot::UnwindKind::Quit) return QuitReason::None;
  if (g_sigint) {
    g_sigint = 0;
    return QuitReason::Interrupt;
  }
  g_quitRequested = true;
  return QuitReason::Requested;
}

// ---------------------------------------------------------------------------
// flagship: native handler that offers restarts interactively

static Value repl_condition_handler(State& vm, u32 base, u32 argc) {
  Value cond = argc > 0 ? vm.stack[base] : ot::nil_v();

  fputs("\nUnhandled condition: ", stderr);
  print_value(vm, cond, stderr);

  // compute-restarts -> array, innermost first (spec 8.5)
  u32 b = vm.stack.len;
  Value restarts = call_named(vm, "compute-restarts", b, 0);
  if (restarts.tag == Tag::Unwind) return restarts;
  ot::Scope sc(vm);
  ot::Slot rS = sc.push(restarts);  // root across allocations below

  // list them
  ot::i64 count = 0;
  for (;; ++count) {
    Value name, desc;
    {
      ot::Scope s2(vm);
      ot::Slot r = s2.push(ot::array_get(rS.get(), count));
      if (ot::is_nil(r.get())) break;
      u32 ab = vm.stack.len;
      vm.push(r.get());
      name = call_named(vm, "restart-name", ab, 1);
      vm.popTo(ab);
      vm.push(r.get());
      desc = call_named(vm, "restart-description", ab, 1);
    }
    // name is a symbol (immediate); desc is used with no allocation between
    // the call above and the prints below (Buf/print_display are C-heap only)
    Buf line;
    line.printf("  [%lld] ", (long long)count);
    if (name.tag != Tag::Unwind) ot::print_display(vm, name, line);
    if (desc.tag != Tag::Unwind && !ot::is_nil(desc)) {
      line.appendCstr(" — ");
      ot::print_display(vm, desc, line);
    }
    fwrite(line.data, 1, line.len, stderr);
    fputc('\n', stderr);
  }

  if (count == 0) {
    fputs("(no active restarts)\n", stderr);
    return ot::nil_v();  // decline; condition unwinds to the REPL loop
  }

  for (;;) {
    fputs("restart #? (or press enter to unwind) ", stderr);
    fflush(stderr);
    char buf[128];
    if (!fgets(buf, sizeof buf, stdin)) return ot::nil_v();
    if (buf[0] == '\n') return ot::nil_v();  // decline
    char* end = nullptr;
    long idx = strtol(buf, &end, 10);
    if (end == buf || idx < 0 || idx >= count) {
      fputs("invalid choice\n", stderr);
      continue;
    }
    u32 ib = vm.stack.len;
    vm.push(ot::array_get(rS.get(), idx));
    // invoke-restart with a restart value unwinds to exactly that restart;
    // the resulting Unwind propagates out of this handler and resumes there.
    return call_named(vm, "invoke-restart", ib, 1);
  }
}

static Value always_true_pred(State& vm, u32, u32) {
  (void)vm;
  return ot::bool_v(true);
}

// ---------------------------------------------------------------------------
// file runner

static int run_file(State& vm, const char* path) {
  FILE* f = fopen(path, "rb");
  if (!f) {
    fprintf(stderr, "otium: cannot open %s\n", path);
    return 1;
  }
  std::string src;
  char chunk[4096];
  size_t n;
  while ((n = fread(chunk, 1, sizeof chunk, f)) > 0) src.append(chunk, n);
  fclose(f);

  Value result = ot::eval_source(vm, src.data(), (u32)src.size(), path);
  if (result.tag == Tag::Unwind) {
    QuitReason reason = take_quit(vm);
    if (reason != QuitReason::None) {
      ot::state_cancel_unwind(vm);
      return reason == QuitReason::Interrupt ? 130 : 0;
    }
    print_value(vm, vm.unwindCondition, stderr);
    return 1;
  }
  return 0;
}

// ---------------------------------------------------------------------------
// REPL

struct ReplEvalContext {
  ot::Slot pred;
  ot::Slot handler;
};

static Value eval_repl_form(State& vm, Value form, void* data) {
  ReplEvalContext& context = *(ReplEvalContext*)data;
  Value pushed = ot::state_push_handler(vm, context.pred.get(), context.handler.get());
  (void)pushed;
  Value result = ot::eval_form(vm, form);
  ot::state_pop_handler(vm);
  return result;
}

static void print_repl_result(State& vm, Value result, u32, void*) {
  // A REPL reports every evaluation result, including nil.
  print_value(vm, result, stdout);
}

// The server protocol uses one control character per direction. A request is
// source text followed by a line containing US; each response ends with RS
// and "ot> ". Source can contain any number of lines and top-level forms.
static constexpr char SERVER_REQUEST_END = '\x1f';
static constexpr const char* SERVER_RESPONSE_END = "\x1eot> ";

static bool read_server_line(std::string& line) {
  line.clear();
  for (;;) {
    int c = fgetc(stdin);
    if (c == EOF) return !line.empty();
    if (c == '\n') return true;
    line.push_back((char)c);
  }
}

static Value eval_server_form(State& vm, Value form, void*) { return ot::eval_form(vm, form); }

static void finish_server_response() {
  fputs(SERVER_RESPONSE_END, stdout);
  fflush(stdout);
}

static void run_server(State& vm) {
  g_replMode = true;
  std::string source;
  std::string line;

  while (read_server_line(line)) {
    if (!(line.size() == 1 && line[0] == SERVER_REQUEST_END)) {
      if (!source.empty()) source.push_back('\n');
      source.append(line);
      continue;
    }

    bool stop = false;
    if (!source.empty()) {
      ot::EvalSourceState sourceState;
      ot::EvalSourcePolicy policy{nullptr, eval_server_form, print_repl_result, &sourceState};
      Value result = ot::eval_source(vm, source.data(), (u32)source.size(), "<server>", policy);
      if (result.tag == Tag::Unwind) {
        QuitReason reason = take_quit(vm);
        if (reason == QuitReason::Interrupt) {
          vm.interruptFlag = false;
          puts("interrupted");
        } else if (reason == QuitReason::Requested) {
          stop = true;
        } else {
          fputs("error: ", stdout);
          print_value(vm, vm.unwindCondition, stdout);
        }
        ot::state_cancel_unwind(vm);
      }
    }

    source.clear();
    if (stop) break;
    finish_server_response();
  }
}

static void run_repl(State& vm) {
  g_replMode = true;
  printf("otium repl — ^C interrupts, ^D exits\n");
  // Both natives stay rooted in these slots for the whole session; read them
  // through the slots at each install — a raw copy would go stale as soon as
  // an eval allocates.
  ot::Slot handlerFn{
      &vm, vm.push(ot::make_native(vm, "repl-condition-handler", repl_condition_handler))};
  ot::Slot predFn{&vm, vm.push(ot::nil_v())};
  predFn.set(ot::make_native(vm, "repl-any-pred", always_true_pred));

  std::string pending;
  for (;;) {
    const char* prompt = pending.empty() ? "ot> " : "..> ";
    char* line = bestlineWithHistory(prompt, "otium");
    if (!line) {
      if (g_sigint) {
        g_sigint = 0;
        vm.interruptFlag = false;
        pending.clear();
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
    if (blank && pending.empty()) {
      bestlineFree(line);
      continue;
    }

    if (!pending.empty()) pending.push_back('\n');
    pending.append(line, len);
    bestlineFree(line);

    bool needMore = false;
    bool stop = false;
    ot::EvalSourceState sourceState;
    ReplEvalContext context{predFn, handlerFn};
    ot::EvalSourcePolicy policy{&context, eval_repl_form, print_repl_result, &sourceState};
    Value result = ot::eval_source(vm, pending.data(), (u32)pending.size(), "<repl>", policy);
    u32 consumed = sourceState.consumed;
    if (result.tag == Tag::Unwind) {
      if (sourceState.readError) {
        if (sourceState.incomplete) {
          ot::state_cancel_unwind(vm);
          needMore = true;
        } else {
          print_value(vm, vm.unwindCondition, stderr);
          ot::state_cancel_unwind(vm);
          consumed = (u32)pending.size();
        }
      } else {
        QuitReason reason = take_quit(vm);
        if (reason == QuitReason::Interrupt) {
          vm.interruptFlag = false;
          puts("interrupted");
        } else if (reason == QuitReason::None) {
          fputs("error: ", stderr);
          print_value(vm, vm.unwindCondition, stderr);
        } else {
          stop = true;
        }
        ot::state_cancel_unwind(vm);
        consumed = (u32)pending.size();
      }
    }
    if (consumed) pending.erase(0, consumed);
    if (stop) break;
    if (needMore) continue;
  }
  bestlineHistoryFree();
  putchar('\n');
}

// ---------------------------------------------------------------------------
// main

struct CliOptions {
  const char* script = nullptr;
  bool repl = false;
  bool server = false;
  u32 maxDepth = 512;
  u32 stackSlots = 4096;
  u32 heapInit = 4u * 1024 * 1024;
  u32 heapMax = 64u * 1024 * 1024;
};

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

static int parse_args(int argc, char** argv, CliOptions& options) {
  bool positionalOnly = false;
  for (int i = 1; i < argc; ++i) {
    const char* arg = argv[i];
    if (!positionalOnly && strcmp(arg, "--") == 0) {
      positionalOnly = true;
    } else if (!positionalOnly && strcmp(arg, "--repl") == 0) {
      options.repl = true;
    } else if (!positionalOnly && strcmp(arg, "--server") == 0) {
      options.server = true;
    } else if (!positionalOnly && strcmp(arg, "--path") == 0) {
      if (++i == argc) {
        fputs("otium: --path requires a directory\n", stderr);
        return 2;
      }
      g_loadPath.push_back(argv[i]);
    } else if (!positionalOnly &&
               (strcmp(arg, "--max-depth") == 0 || strcmp(arg, "--stack-slots") == 0 ||
                strcmp(arg, "--heap-init") == 0 || strcmp(arg, "--heap-max") == 0)) {
      if (++i == argc) {
        fprintf(stderr, "otium: %s requires a value\n", arg);
        return 2;
      }
      u32* target = strcmp(arg, "--max-depth") == 0     ? &options.maxDepth
                    : strcmp(arg, "--stack-slots") == 0 ? &options.stackSlots
                    : strcmp(arg, "--heap-init") == 0   ? &options.heapInit
                                                        : &options.heapMax;
      if (!parse_u32_arg(arg, argv[i], target)) return 2;
    } else if (!positionalOnly && (strcmp(arg, "--help") == 0 || strcmp(arg, "-h") == 0)) {
      print_usage(stdout);
      return 1;
    } else if (!positionalOnly && arg[0] == '-' && arg[1] != '\0') {
      fprintf(stderr, "otium: unknown option %s\n", arg);
      fputs("Try 'otium --help' for more information.\n", stderr);
      return 2;
    } else if (!options.script) {
      options.script = arg;
    } else {
      fprintf(stderr, "otium: unexpected argument %s\n", arg);
      return 2;
    }
  }
  if (options.repl && options.server) {
    fputs("otium: --repl and --server cannot be used together\n", stderr);
    return 2;
  }
  if (options.heapInit < 1024) {
    fputs("otium: --heap-init must be at least 1024 bytes\n", stderr);
    return 2;
  }
  if (options.heapMax < options.heapInit) {
    fputs("otium: --heap-max must be at least --heap-init\n", stderr);
    return 2;
  }
  return 0;
}

int main(int argc, char** argv) {
  CliOptions options;
  int parseStatus = parse_args(argc, argv, options);
  if (parseStatus != 0) return parseStatus == 1 ? 0 : parseStatus;

  if (const char* env = getenv("OTIUM_PATH")) {
    std::string s(env);
    size_t start = 0;
    while (start <= s.size()) {
      size_t colon = s.find(':', start);
      std::string part =
          s.substr(start, colon == std::string::npos ? std::string::npos : colon - start);
      if (!part.empty()) g_loadPath.push_back(part);
      if (colon == std::string::npos) break;
      start = colon + 1;
    }
  }
  if (g_loadPath.empty()) g_loadPath.push_back(".");

  ot::StateConfig cfg;
  cfg.heapBytes = options.heapInit;
  cfg.stackSlots = options.stackSlots;
  cfg.maxDepth = options.maxDepth;
  cfg.heapMaxBytes = options.heapMax;
  State* vm = State::create(cfg);
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
  ot::register_demo_extension(*vm);
#endif
#ifdef OT_EXT_RAYLIB
  ot::register_raylib_extension(*vm);
#endif

  struct sigaction sa;
  memset(&sa, 0, sizeof sa);
  sa.sa_handler = on_sigint;
  sigaction(SIGINT, &sa, nullptr);

  int status = 0;
  if (options.script) status = run_file(*vm, options.script);
  if (status == 0 && !g_quitRequested && options.server) {
    run_server(*vm);
  } else if (status == 0 && !g_quitRequested && (options.repl || !options.script)) {
    run_repl(*vm);
  }

  vm->destroy();
  g_vm = nullptr;
  return status;
}
