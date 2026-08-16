// repl/main.cpp — Otium CLI + REPL (STK-18)
//
// The one file where std headers and OS APIs are welcome. Everything else is
// written strictly against agent-docs/interfaces.md; spots where the contract
// is silent are marked // INTEGRATION:.

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <csignal>
#include <cctype>
#include <string>
#include <vector>

#include "bestline.h"
#include "common.hpp"
#include "vec.hpp"
#include "value.hpp"
#include "heap.hpp"
#include "vm.hpp"  // INTEGRATION: expected per interfaces.md (Vm, VmConfig, NativeFn)
#include "reader.hpp"
#include "printer.hpp"  // INTEGRATION: expected per interfaces.md (print_repr)
#include "eval.hpp"     // INTEGRATION: expected per interfaces.md (eval_form, apply)
#include "ns.hpp"       // INTEGRATION: expected per interfaces.md (ns_resolve)

// vm_push_handler/vm_pop_handler come from vm.hpp; make_native from eval.hpp.

using ot::Buf;
using ot::Tag;
using ot::u32;
using ot::Value;
using ot::Vm;

// ---------------------------------------------------------------------------
// globals

static Vm* g_vm = nullptr;
static volatile sig_atomic_t g_sigint = 0;
static bool g_replMode = false;
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

static Value resolve_named(Vm& vm, const char* name) {
  u32 id = vm.intern.intern(name, (u32)strlen(name));
  return ot::ns_resolve(vm, ot::symbol_v(id));
}

// call an otium function by name with argc args already pushed at `base`
static Value call_named(Vm& vm, const char* name, u32 base, u32 argc) {
  Value fn = resolve_named(vm, name);
  if (fn.tag == Tag::Unwind) return fn;
  return ot::apply(vm, fn, base, argc);
}

static void print_value(Vm& vm, Value v, FILE* to) {
  Buf out;
  ot::print_repr(vm, v, out);
  fwrite(out.data, 1, out.len, to);
  fputc('\n', to);
}

static bool was_interrupt(Vm& vm) {
  // The evaluator marks ^C (and any future (quit)) as UnwindKind::Quit.
  if (vm.unwindKind == ot::UnwindKind::Quit) {
    g_sigint = 0;
    return true;
  }
  return false;
}

// ---------------------------------------------------------------------------
// flagship: native handler that offers restarts interactively

static Value repl_condition_handler(Vm& vm, u32 base, u32 argc) {
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

static Value always_true_pred(Vm& vm, u32, u32) {
  (void)vm;
  return ot::bool_v(true);
}

// ---------------------------------------------------------------------------
// file runner

static int run_file(Vm& vm, const char* path) {
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

  ot::Reader rd(vm, src.data(), (u32)src.size(), path);
  for (;;) {
    Value form = rd.next();
    if (form.tag == Tag::Unwind) {
      print_value(vm, vm.unwindCondition, stderr);
      return 1;
    }
    if (rd.atEof()) break;
    Value result = ot::eval_form(vm, form);
    if (result.tag == Tag::Unwind) {
      if (was_interrupt(vm)) return 130;
      print_value(vm, vm.unwindCondition, stderr);
      return 1;
    }
  }
  return 0;
}

// ---------------------------------------------------------------------------
// REPL

static void run_repl(Vm& vm) {
  g_replMode = true;
  printf("otium repl — ^C interrupts, ^D exits\n");
  // PoC note: one complete form per line. Multi-line accumulation would retry
  // the read on an unterminated-form error; kept simple for now.
  // Both natives stay rooted in these slots for the whole session; read them
  // through the slots at each install — a raw copy would go stale as soon as
  // an eval allocates.
  ot::Slot handlerFn{&vm, vm.push(ot::make_native(vm, "repl-condition-handler",
                                                  repl_condition_handler))};
  ot::Slot predFn{&vm, vm.push(ot::nil_v())};
  predFn.set(ot::make_native(vm, "repl-any-pred", always_true_pred));

  for (;;) {
    char* line = bestlineWithHistory("otium> ", "otium");
    if (!line) {
      if (g_sigint) {
        g_sigint = 0;
        vm.interruptFlag = false;
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
    if (blank) {
      bestlineFree(line);
      continue;
    }

    ot::Reader rd(vm, line, (u32)len, "<repl>");
    for (;;) {
      Value form = rd.next();
      if (form.tag == Tag::Unwind) {
        print_value(vm, vm.unwindCondition, stderr);
        break;
      }
      if (rd.atEof()) break;

      // Install the interactive restart-offering handler around this eval.
      Value ph = ot::vm_push_handler(vm, predFn.get(), handlerFn.get());
      (void)ph;
      Value result = ot::eval_form(vm, form);
      ot::vm_pop_handler(vm);

      if (result.tag == Tag::Unwind) {
        if (was_interrupt(vm)) {
          vm.interruptFlag = false;
          puts("interrupted");
        } else {
          fputs("error: ", stderr);
          print_value(vm, vm.unwindCondition, stderr);
        }
        ot::vm_cancel_unwind(vm);
        break;
      }
      print_value(vm, result, stdout);  // PoC: print everything, incl. nil
    }
    bestlineFree(line);
  }
  bestlineHistoryFree();
  putchar('\n');
}

// ---------------------------------------------------------------------------
// main

struct CliOptions {
  const char* script = nullptr;
  bool repl = false;
};

static void print_usage(FILE* to) {
  fputs("Usage: otium [OPTIONS] [FILE]\n"
        "Run FILE, or start a REPL when no FILE is given.\n"
        "\n"
        "Options:\n"
        "  --repl       Start a REPL after loading FILE\n"
        "  --path DIR   Add DIR to the module search path (repeatable)\n"
        "  -h, --help   Show this help\n",
        to);
}

static int parse_args(int argc, char** argv, CliOptions& options) {
  bool positionalOnly = false;
  for (int i = 1; i < argc; ++i) {
    const char* arg = argv[i];
    if (!positionalOnly && strcmp(arg, "--") == 0) {
      positionalOnly = true;
    } else if (!positionalOnly && strcmp(arg, "--repl") == 0) {
      options.repl = true;
    } else if (!positionalOnly && strcmp(arg, "--path") == 0) {
      if (++i == argc) {
        fputs("otium: --path requires a directory\n", stderr);
        return 2;
      }
      g_loadPath.push_back(argv[i]);
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

  ot::VmConfig cfg;
  cfg.heapBytes = 4 * 1024 * 1024;
  cfg.stackSlots = 4096;
  cfg.maxDepth = 512;
  Vm* vm = Vm::create(cfg);
  if (!vm) {
    fputs("otium: vm creation failed\n", stderr);
    return 1;
  }
  g_vm = vm;
  vm->writeFn = host_write;
  vm->writeUd = nullptr;
  vm->loadFn = host_load;
  vm->loadUd = nullptr;

  struct sigaction sa;
  memset(&sa, 0, sizeof sa);
  sa.sa_handler = on_sigint;
  sigaction(SIGINT, &sa, nullptr);

  int status = 0;
  if (options.script) status = run_file(*vm, options.script);
  if (status == 0 && (options.repl || !options.script)) {
    run_repl(*vm);
  }

  vm->destroy();
  g_vm = nullptr;
  return status;
}
