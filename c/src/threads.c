#include "internal.h"

/* Green threads: many threads of Otium code on one native thread. See
 * scratch/green-threads.md and scratch/segmented-stacks.md.
 *
 * Native code runs on Otium stacks, one per thread, and all C runs on the
 * one C stack (native.c). A thread is switched only when it has no C frames
 * of its own: when a builtin it called parks, at a poll, or when it
 * finishes, and never while C is waiting on Otium code it called. So a
 * thread that isn't running is its saved exec block, its Otium stack and
 * where to continue: the start of the thread, the C call it was preempted
 * in, or the builtin call that parked, which is dispatched again when the
 * thread wakes. Waiting builtins are written for that: they mark the thread
 * waiting and return OTI_PARKED, and when called again they see the wake
 * value (retried) and return it or check again.
 *
 * The scheduler runs on the C stack: it saves the running thread's exec,
 * loads the next one's, raises in it whatever is pending (^C on the main
 * thread, a kill, an exit, a deadlock), runs an interrupt thunk, and jumps
 * into it with oti_native_resume.
 *
 * The run queue is FIFO, timers are sorted by deadline, and a waiting
 * thread is on at most one wait queue and the timer list; waking takes it
 * off both, so a timeout and a wake can't both happen. Preemption is a
 * ticker that trips the native stack limit every millisecond while another
 * thread is runnable or sleeping; the poll it trips yields. With
 * OTIUM_SCHED=seed[:slice] the scheduler is deterministic instead, for
 * tests: every poll counts towards a slice of that many polls (random from
 * the seed when not given), the next thread is chosen at random, and time
 * is virtual, jumping to the next deadline when nothing can run. */

#ifdef OTIUM_NATIVE

#include <pthread.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <time.h>
#include <unistd.h>

typedef enum { T_RUNNABLE, T_RUNNING, T_WAITING, T_DONE, T_FAILED, T_KILLED } oti_thread_state;

/* How a thread that isn't running continues. */
typedef enum { R_BEGIN, R_REDISPATCH, R_RETURN } oti_resume;

typedef struct oti_thread oti_thread;

/* A wait queue: threads parked until something wakes them. */
typedef struct {
  oti_thread *head, *tail;
} oti_waitq;

struct oti_thread {
  /* Saved while another thread runs. */
  oti_exec exec;
  oti_resume resume;
  ot_value object, entry, args, wake_value, kill_reason;
  /* A thunk thread/interrupt! asked this thread to run, or nil. */
  ot_value interrupt;
  /* Monitors: (watcher . ref) pairs to send an exit message when it ends. */
  ot_value watchers;
  oti_thread_state state;
  /* Links on the run queue or the wait queue it is parked on. */
  oti_thread *next, *prev;
  oti_waitq* queue;
  uint64_t deadline;
  /* woken: the parked builtin call is being dispatched again. */
  bool timed, daemon, kill_pending, mail_waiting, woken;
  /* The atomic depth %park released for its wait. */
  size_t saved_atomic;
  ot_value* mailbox;
  size_t mailbox_count, mailbox_capacity, cursor;
  oti_waitq joiners;
};

struct oti_threads {
  /* Live threads, including the running one. */
  oti_thread** all;
  size_t count, capacity;
  /* NULL while a finished thread hands over to the next. */
  oti_thread *current, *primordial;
  oti_waitq run;
  oti_thread** timers;
  size_t timer_count, timer_capacity;
  /* Wait queues made for Otium code, freed with the runtime. */
  oti_waitq** queues;
  size_t queue_count, queue_capacity;
  volatile sig_atomic_t preempt;
  pthread_t ticker;
  volatile bool stop;
  bool ticking;
  /* The deterministic test scheduler. */
  bool test;
  uint64_t seed, slice, budget, now;
};

typedef struct {
  oti_object_header header;
  ot_value name, result, status;
  /* NULL once the thread has finished. */
  oti_thread* thread;
} oti_thread_obj;

OTI_LAYOUT(oti_thread_obj, 3, thread);

typedef struct {
  oti_object_header header;
  oti_waitq* queue;
} oti_waitq_obj;

OTI_LAYOUT(oti_waitq_obj, 0, queue);

/* --- Time ----------------------------------------------------------------- */

static uint64_t now(const oti_threads* ts) {
  if (ts && ts->test) return ts->now;
  struct timespec t;
  clock_gettime(CLOCK_MONOTONIC, &t);
  return (uint64_t)t.tv_sec * 1000000000u + (uint64_t)t.tv_nsec;
}

static void pause_for(uint64_t ns) {
  struct timespec pause = {(time_t)(ns / 1000000000u), (long)(ns % 1000000000u)};
  nanosleep(&pause, NULL);
}

/* A deadline seconds from now, or 0 for none (nil or #f). */
static bool deadline_after(ot_state* rt, ot_value seconds, uint64_t* deadline) {
  double s;
  if (seconds == OT_NIL || seconds == OT_FALSE) {
    *deadline = 0;
    return true;
  }
  if (!oti_number(rt, seconds, &s) || s != s) return oti_error(rt, "expected a number of seconds");
  if (s < 0) s = 0;
  if (s > 1e9) s = 1e9;
  *deadline = now(rt->threads) + (uint64_t)(s * 1e9) + 1;
  return true;
}

/* A deadline %thread-deadline made, or nil for none. Waiting builtins take
 * absolute deadlines, since a parked call is dispatched again. */
static bool deadline_arg(ot_state* rt, ot_value v, uint64_t* deadline) {
  if (v == OT_NIL || v == OT_FALSE) {
    *deadline = 0;
    return true;
  }
  if (!ot_is_int(v) || ot_to_int(v) <= 0) return oti_error(rt, "expected a deadline from %thread-deadline");
  *deadline = (uint64_t)ot_to_int(v);
  return true;
}

/* --- Queues -------------------------------------------------------------- */

static void enqueue(oti_waitq* q, oti_thread* t) {
  t->next = NULL;
  t->prev = q->tail;
  if (q->tail) q->tail->next = t;
  else q->head = t;
  q->tail = t;
}

static void unlink_from(oti_waitq* q, oti_thread* t) {
  if (t->prev) t->prev->next = t->next;
  else q->head = t->next;
  if (t->next) t->next->prev = t->prev;
  else q->tail = t->prev;
  t->next = t->prev = NULL;
}

static oti_thread* dequeue(oti_waitq* q) {
  oti_thread* t = q->head;
  if (t) unlink_from(q, t);
  return t;
}

static size_t queue_length(const oti_waitq* q) {
  size_t n = 0;
  for (const oti_thread* t = q->head; t; t = t->next) n++;
  return n;
}

static bool add_timer(ot_state* rt, oti_thread* t) {
  oti_threads* ts = rt->threads;
  if (!oti_grow(rt, (void**)&ts->timers, &ts->timer_capacity, ts->timer_count + 1,
                sizeof(oti_thread*)))
    return false;
  size_t i = ts->timer_count++;
  while (i && ts->timers[i - 1]->deadline > t->deadline) {
    ts->timers[i] = ts->timers[i - 1];
    i--;
  }
  ts->timers[i] = t;
  t->timed = true;
  return true;
}

static void remove_timer(oti_threads* ts, oti_thread* t) {
  if (!t->timed) return;
  size_t i = 0;
  while (i < ts->timer_count && ts->timers[i] != t) i++;
  if (i < ts->timer_count) {
    memmove(ts->timers + i, ts->timers + i + 1, (ts->timer_count - i - 1) * sizeof(oti_thread*));
    ts->timer_count--;
  }
  t->timed = false;
}

/* Make a parked thread runnable, its wait ending with value. */
static void wake(ot_state* rt, oti_thread* t, ot_value value) {
  oti_threads* ts = rt->threads;
  if (t->state != T_WAITING) return;
  if (t->queue) {
    unlink_from(t->queue, t);
    t->queue = NULL;
  }
  remove_timer(ts, t);
  t->mail_waiting = false;
  t->wake_value = value;
  t->state = T_RUNNABLE;
  enqueue(&ts->run, t);
}

/* --- Thread objects and the registry -------------------------------------- */

static oti_thread_obj* as_thread(ot_value v) { return OTI_CAST(oti_thread_obj, K_THREAD, v); }

static ot_value keyword(ot_state* rt, const char* name) {
  return oti_intern(rt, K_KEYWORD, name, strlen(name));
}

static const char* state_name(oti_thread_state s) {
  switch (s) {
    case T_RUNNABLE:
    case T_RUNNING: return ":runnable";
    case T_WAITING: return ":waiting";
    case T_DONE: return ":done";
    case T_FAILED: return ":failed";
    default: return ":killed";
  }
}

static void visit_thread(ot_state* rt, oti_thread* t, oti_root_visitor visit, void* context) {
  visit(context, &t->object);
  visit(context, &t->entry);
  visit(context, &t->args);
  visit(context, &t->wake_value);
  visit(context, &t->kill_reason);
  visit(context, &t->interrupt);
  visit(context, &t->watchers);
  for (size_t i = 0; i < t->mailbox_count; i++) visit(context, &t->mailbox[i]);
  if (t != rt->threads->current && t->state < T_DONE) oti_visit_exec(rt, &t->exec, visit, context);
}

void oti_visit_threads(ot_state* rt, oti_root_visitor visit, void* context) {
  oti_threads* ts = rt->threads;
  if (!ts) return;
  for (size_t i = 0; i < ts->count; i++) visit_thread(rt, ts->all[i], visit, context);
}

static void free_exec(ot_state* rt, oti_exec* x) {
  oti_native_stack_free(rt, &x->native);
  free(x->stack);
  free(x->frames);
  free(x->handlers);
  free(x->handler_previous);
  free(x->restarts);
  free(x->restart_scopes);
  free(x->cleanups);
  free(x->cleanup_states);
  free(x->params);
  free(x->tries);
  free(x->native.args);
  for (size_t i = 0; i < x->unwind_count; i++) oti_release_error(&x->unwinds[i].error);
  free(x->unwinds);
  oti_release_error(&x->error);
}

static void free_thread(oti_thread* t) {
  free(t->mailbox);
  free(t);
}

static void unregister(oti_threads* ts, oti_thread* t) {
  for (size_t i = 0; i < ts->count; i++)
    if (ts->all[i] == t) {
      ts->all[i] = ts->all[--ts->count];
      return;
    }
}

/* --- Preemption ---------------------------------------------------------- */

/* The ticker asks the running thread to yield every millisecond while
 * another is runnable or sleeping, by tripping its stack limit; the poll
 * does the rest, waking sleepers that are due. It only reads and writes
 * words, so it needs no lock. */
static void* ticker(void* data) {
  ot_state* rt = data;
  oti_threads* ts = rt->threads;
  while (!ts->stop) {
    pause_for(1000000);
    if (ts->run.head || ts->timer_count) {
      ts->preempt = 1;
      rt->native.limit = UINTPTR_MAX;
    }
  }
  return NULL;
}

static uint64_t next_random(oti_threads* ts) {
  /* xorshift64* */
  uint64_t x = ts->seed;
  x ^= x >> 12;
  x ^= x << 25;
  x ^= x >> 27;
  ts->seed = x;
  return x * 2685821657736338717u;
}

static void new_slice(oti_threads* ts) {
  if (ts->test) ts->budget = ts->slice ? ts->slice : 1 + next_random(ts) % 50;
}

/* The scheduler and the primordial thread's record, made on first use, so a
 * program without threads has none of this. The ticker starts with the
 * first spawn. */
static bool setup(ot_state* rt) {
  if (rt->threads) return true;
  if (!rt->native_mode) return oti_error(rt, "threads need native code (run without --c.vm)");
  oti_threads* ts = calloc(1, sizeof(*ts));
  oti_thread* primordial = calloc(1, sizeof(*primordial));
  if (!ts || !primordial) {
    free(ts);
    free(primordial);
    return oti_error(rt, "out of host memory");
  }
  ts->all = malloc(8 * sizeof(oti_thread*));
  if (!ts->all) {
    free(ts);
    free(primordial);
    return oti_error(rt, "out of host memory");
  }
  ts->capacity = 8;
  rt->threads = ts;
  primordial->object = primordial->entry = primordial->args = OT_NIL;
  primordial->wake_value = primordial->kill_reason = OT_NIL;
  primordial->interrupt = OT_NIL;
  primordial->watchers = OT_EMPTY;
  primordial->state = T_RUNNING;
  ts->primordial = ts->current = primordial;
  ts->all[ts->count++] = primordial;
  const char* sched = getenv("OTIUM_SCHED");
  if (sched && *sched) {
    ts->test = true;
    char* end;
    ts->seed = strtoull(sched, &end, 10) * 2654435761u + 88172645463325252u;
    ts->slice = *end == ':' ? strtoull(end + 1, NULL, 10) : 0;
    ts->now = 1;
    new_slice(ts);
  }
  ot_value object = oti_alloc(rt, K_THREAD, 3, OTI_BYTES(oti_thread_obj, 3));
  if (!object) return false;
  as_thread(object)->name = OT_NIL;
  as_thread(object)->result = as_thread(object)->status = OT_NIL;
  as_thread(object)->thread = primordial;
  primordial->object = object;
  ot_value name = keyword(rt, ":main");
  if (!name) return false;
  as_thread(primordial->object)->name = name;
  oti_write(rt, primordial->object);
  return true;
}

void oti_threads_destroy(ot_state* rt) {
  oti_threads* ts = rt->threads;
  if (!ts) return;
  ts->stop = true;
  if (ts->ticking) pthread_join(ts->ticker, NULL);
  /* The running thread's exec is rt->exec, which the runtime frees. */
  for (size_t i = 0; i < ts->count; i++) {
    oti_thread* t = ts->all[i];
    if (t != ts->current) free_exec(rt, &t->exec);
    free_thread(t);
  }
  for (size_t i = 0; i < ts->queue_count; i++) free(ts->queues[i]);
  free(ts->queues);
  free(ts->all);
  free(ts->timers);
  free(ts);
  rt->threads = NULL;
}

size_t oti_thread_count(ot_state* rt) { return rt->threads ? rt->threads->count : 1; }

bool oti_threads_grow_args(ot_state* rt) {
  oti_threads* ts = rt->threads;
  if (!ts) return true;
  size_t capacity = rt->native.args_capacity;
  for (size_t i = 0; i < ts->count; i++) {
    oti_native_exec* n = &ts->all[i]->exec.native;
    if (ts->all[i] == ts->current || n->args_capacity >= capacity) continue;
    size_t old = n->args_capacity;
    if (!oti_grow(rt, (void**)&n->args, &n->args_capacity, capacity, sizeof(ot_value))) return false;
    for (size_t j = old; j < n->args_capacity; j++) n->args[j] = OT_NIL;
  }
  return true;
}

/* --- Switching ------------------------------------------------------------ */

/* Whether the running thread can be switched out: native code is running
 * and C isn't waiting on Otium code it called. */
static bool can_switch(ot_state* rt) {
  return rt->native_c_base && rt->native_c_sp == rt->native_c_base;
}

static bool loading_module(ot_state* rt) { return oti_module_depth(rt) > 0; }

/* Wake threads whose deadline has passed, with :timeout. */
static void expire(ot_state* rt) {
  oti_threads* ts = rt->threads;
  if (!ts->timer_count || ts->timers[0]->deadline > now(ts)) return;
  ot_value timeout = keyword(rt, ":timeout");
  uint64_t t = now(ts);
  while (ts->timer_count && ts->timers[0]->deadline <= t) wake(rt, ts->timers[0], timeout);
}

static oti_thread* pick(oti_threads* ts) {
  if (!ts->test || !ts->run.head) return dequeue(&ts->run);
  size_t k = (size_t)(next_random(ts) % queue_length(&ts->run));
  oti_thread* t = ts->run.head;
  while (k--) t = t->next;
  unlink_from(&ts->run, t);
  return t;
}

/* What a thread checks as it continues after a wait or a preemption. False
 * when it must unwind instead: ^C on the main thread, an interrupt thunk
 * that failed, a kill, another thread's exit, or a deadlock. */
static bool checks(ot_state* rt, oti_thread* t) {
  oti_threads* ts = rt->threads;
  if (t == ts->primordial && !oti_interrupt_point(rt)) return false;
  /* A thunk from thread/interrupt! runs here, on top of whatever this thread
   * was doing, unless it is in an atomic section. */
  if (t->interrupt != OT_NIL && !rt->atomic && !t->kill_pending) {
    ot_value thunk = t->interrupt;
    t->interrupt = OT_NIL;
    if (!oti_native_invoke(rt, thunk, 0)) return false;
  }
  if (t->kill_pending && !rt->interrupted) {
    t->kill_pending = false;
    rt->interrupted = true;
    return oti_error(rt, "thread killed");
  }
  if (rt->exit_pending && t == ts->primordial) {
    if (!oti_failed(rt)) oti_error(rt, "exit requested");
    return false;
  }
  if (t->resume == R_REDISPATCH && t->wake_value == keyword(rt, ":deadlock"))
    return oti_coded_error(rt, "E0503", "deadlock: no thread can run and none is sleeping");
  return true;
}

static _Noreturn void finish(ot_state* rt, ot_value result);

/* Continue t, whose exec is now rt->exec. */
static _Noreturn void enter(ot_state* rt, oti_thread* t) {
  if (t->resume == R_BEGIN) {
    if (t->kill_pending) {
      rt->interrupted = true;
      finish(rt, 0);
    }
    size_t argc = 0;
    for (ot_value p = t->args; oti_kind(p, K_PAIR); p = oti_cdr(p)) argc++;
    if (!oti_native_args(rt, argc)) finish(rt, 0);
    size_t i = 0;
    for (ot_value p = t->args; oti_kind(p, K_PAIR); p = oti_cdr(p)) rt->native.args[i++] = oti_car(p);
    rt->native.callee = t->entry;
    rt->native.argc = argc;
    t->entry = t->args = OT_NIL;
    t->resume = R_RETURN;
    /* As oti_native_invoke counts entries for try and cleanup records. */
    rt->frame_count = 1;
    oti_native_resume(rt, rt->native.stack_top, (uintptr_t)oti_thread_begin,
                      oti_native_entry_of(rt, rt->native.callee));
  }
  /* The checks can run Otium code, whose calls go through native.callee and
   * native.args, so a parked call waits on the value stack meanwhile. */
  size_t base = rt->sp, argc = rt->native.args_live;
  bool parked = t->resume == R_REDISPATCH && oti_stack(rt, base + argc + 1);
  if (parked) {
    rt->stack[base] = rt->native.callee;
    memcpy(rt->stack + base + 1, rt->native.args, argc * sizeof(ot_value));
    rt->sp = base + argc + 1;
    rt->native.args_live = 0;
  }
  bool ok = checks(rt, t);
  if (parked && ok) {
    rt->native.callee = rt->stack[base];
    memcpy(rt->native.args, rt->stack + base + 1, argc * sizeof(ot_value));
    rt->native.argc = rt->native.args_live = argc;
    rt->sp = base;
  }
  if (!ok) {
    /* The parked call is abandoned. */
    rt->sp = base;
    rt->native.args_live = 0;
    oti_native_raise(rt);
  }
  if (t->resume == R_REDISPATCH) {
    t->woken = true;
    /* Nothing collects between here and dispatch copying them. */
    rt->native.args_live = 0;
    oti_native_resume(rt, rt->native.sp, oti_native_redispatch(rt), 0);
  }
  oti_native_resume(rt, rt->native_c_sp, (uintptr_t)oti_c_resume, OT_NIL);
}

/* Run something else: the running thread has been queued to run again,
 * parked, or (current NULL) finished. Sleeps when only timers can make
 * progress. With nothing runnable and nothing timed, the primordial thread,
 * which is then waiting, wakes with :deadlock. */
static _Noreturn void schedule(ot_state* rt) {
  oti_threads* ts = rt->threads;
  if (ts->current) {
    oti_native_stack_trim(rt, &rt->native);
    ts->current->exec = rt->exec;
  }
  for (;;) {
    expire(rt);
    oti_thread* next = pick(ts);
    if (next) {
      ts->preempt = 0;
      new_slice(ts);
      next->state = T_RUNNING;
      ts->current = next;
      rt->exec = next->exec;
      /* Under the test scheduler every poll counts. */
      if (ts->test) rt->native.limit = UINTPTR_MAX;
      enter(rt, next);
    }
    if (ts->timer_count) {
      uint64_t deadline = ts->timers[0]->deadline, t = now(ts);
      if (ts->test) ts->now = deadline;
      else if (deadline > t) pause_for(deadline - t);
      continue;
    }
    if (ts->primordial->state != T_WAITING) abort();
    wake(rt, ts->primordial, keyword(rt, ":deadlock"));
  }
}

_Noreturn void oti_thread_wait(ot_state* rt) {
  rt->threads->current->resume = R_REDISPATCH;
  rt->native.args_live = rt->native.argc;
  schedule(rt);
}

/* Park the running thread on q, when given, and until deadline, when not
 * 0. The waiting builtin returns what this returns: OTI_PARKED, or 0 when
 * it can't wait here. */
static ot_value wait_on(ot_state* rt, oti_waitq* q, uint64_t deadline) {
  oti_thread* me = rt->threads->current;
  if (!can_switch(rt)) {
    if (loading_module(rt))
      oti_coded_error(rt, "E0502", "cannot wait for another thread while loading a module");
    else oti_coded_error(rt, "E0506", "cannot wait for another thread inside a call from C");
    return 0;
  }
  me->state = T_WAITING;
  me->wake_value = OT_NIL;
  if (q) {
    me->queue = q;
    enqueue(q, me);
  }
  if (deadline) {
    me->deadline = deadline;
    if (!add_timer(rt, me)) {
      if (q) unlink_from(q, me);
      me->queue = NULL;
      me->state = T_RUNNING;
      return 0;
    }
  }
  return OTI_PARKED;
}

/* Whether this builtin call is a parked one dispatched again, and the value
 * its wait ended with. Every waiting builtin calls this before anything
 * else, so the flag can't outlive the call it belongs to. */
static bool retried(ot_state* rt, ot_value* wake_value) {
  oti_thread* me = rt->threads->current;
  if (!me->woken) return false;
  me->woken = false;
  if (wake_value) *wake_value = me->wake_value;
  return true;
}

/* An interrupt such as ^C is for the main thread. Another thread that finds
 * one pending wakes the main thread, if it is waiting, and yields to it,
 * and the main thread takes the interrupt as it continues. True when the
 * running thread is the main one. */
bool oti_thread_foreground(ot_state* rt) {
  oti_threads* ts = rt->threads;
  if (ts->current == ts->primordial) return true;
  wake(rt, ts->primordial, OT_NIL);
  ts->preempt = 1;
  return false;
}

/* The poll found its limit tripped: yield when the ticker or the test
 * scheduler's slice says so and the thread can be switched. */
bool oti_thread_poll(ot_state* rt) {
  oti_threads* ts = rt->threads;
  if (ts->test) {
    /* Every poll counts; keep the limit tripped so the next one does too. */
    rt->native.limit = UINTPTR_MAX;
    if (ts->budget && --ts->budget) return true;
    ts->preempt = 1;
  }
  if (!ts->preempt || rt->atomic || !can_switch(rt)) return true;
  ts->preempt = 0;
  if (!ts->run.head && !ts->timer_count) return true;
  oti_thread* me = ts->current;
  me->state = T_RUNNABLE;
  enqueue(&ts->run, me);
  me->resume = R_RETURN;
  schedule(rt);
}

/* --- Starting and finishing threads -------------------------------------- */

static void notify_watchers(ot_state* rt, ot_value object, ot_value watchers, bool normal);

/* A thread's run ended with result, or 0 after it failed or was killed.
 * Called on the C stack, by oti_thread_end, or by enter for a thread killed
 * before it started. */
_Noreturn void oti_thread_finished(ot_state* rt, ot_value result) { finish(rt, result); }

static _Noreturn void finish(ot_state* rt, ot_value result) {
  oti_threads* ts = rt->threads;
  oti_thread* me = ts->current;
  rt->frame_count = 0;
  me->entry = me->args = OT_NIL;
  /* The thread stays running until it is done: reporting a failure runs
   * Otium code. */
  oti_thread_state end;
  if (result) {
    end = T_DONE;
    as_thread(me->object)->result = result;
  } else if (rt->interrupted && !rt->exit_pending) {
    end = T_KILLED;
    as_thread(me->object)->result = me->kill_reason;
  } else if (rt->exit_pending) {
    end = T_DONE;
  } else {
    end = T_FAILED;
    /* The failure goes to stderr, and its condition to joiners. */
    ot_report_error(rt, NULL, stderr);
    ot_value condition = oti_error_condition(rt);
    as_thread(me->object)->result = condition ? condition : OT_NIL;
  }
  oti_write(rt, me->object);
  me->state = end;
  ot_value status = keyword(rt, state_name(me->state));
  as_thread(me->object)->status = status;
  oti_write(rt, me->object);
  as_thread(me->object)->thread = NULL;
  while (me->joiners.head) wake(rt, me->joiners.head, OT_TRUE);
  ot_value watchers = me->watchers;
  me->watchers = OT_EMPTY;
  notify_watchers(rt, me->object, watchers, me->state == T_DONE);
  /* exit from any thread ends the process: the primordial thread unwinds. */
  if (rt->exit_pending) {
    oti_thread* primordial = ts->primordial;
    if (primordial->state == T_RUNNABLE) unlink_from(&ts->run, primordial);
    primordial->state = T_WAITING;
    wake(rt, primordial, OT_NIL);
  }
  unregister(ts, me);
  /* Nothing runs on its stack any more: this is the C stack. */
  free_exec(rt, &rt->exec);
  memset(&rt->exec, 0, sizeof(rt->exec));
  free_thread(me);
  ts->current = NULL;
  schedule(rt);
}

/* Send each (watcher . ref) in watchers {:exit object :ref ref :reason
 * reason}: :normal when the thread ended normally, else its result: the kill
 * reason or the condition. */
static void notify_watchers(ot_state* rt, ot_value object, ot_value watchers, bool normal) {
  /* The thread, the watchers left, the message, its reason and keys. */
  ot_value live[] = {object, watchers, OT_NIL, OT_NIL, OT_NIL, OT_NIL, OT_NIL};
  oti_roots roots;
  oti_roots_push(&rt->heap, &roots, live, 7);
  live[3] = normal ? keyword(rt, ":normal") : as_thread(live[0])->result;
  live[4] = keyword(rt, ":exit");
  live[5] = keyword(rt, ":ref");
  live[6] = keyword(rt, ":reason");
  bool ok = live[3] && live[4] && live[5] && live[6];
  for (; ok && oti_kind(live[1], K_PAIR); live[1] = oti_cdr(live[1])) {
    if (!as_thread(oti_car(oti_car(live[1])))->thread) continue;
    live[2] = oti_table_new(rt);
    bool made = live[2] && oti_table_set(rt, live[2], live[4], live[0]) &&
                oti_table_set(rt, live[2], live[5], oti_cdr(oti_car(live[1]))) &&
                oti_table_set(rt, live[2], live[6], live[3]);
    oti_thread* watcher = as_thread(oti_car(oti_car(live[1])))->thread;
    if (made && watcher &&
        oti_grow(rt, (void**)&watcher->mailbox, &watcher->mailbox_capacity,
                 watcher->mailbox_count + 1, sizeof(ot_value))) {
      watcher->mailbox[watcher->mailbox_count++] = live[2];
      if (watcher->mail_waiting) wake(rt, watcher, OT_TRUE);
    }
  }
  oti_roots_pop(&rt->heap, &roots);
  oti_clear_error(rt);
}

/* (%spawn name f args): start a thread running (apply f args). */
static ot_value spawn_builtin(ot_state* rt, size_t first, size_t count) {
  if (count != 3) return oti_fail(rt, "wrong number of arguments");
  if (!setup(rt)) return 0;
  oti_threads* ts = rt->threads;
  if (!oti_procedure(rt->stack[first + 1])) {
    oti_type_error(rt, "spawn", "a function", rt->stack[first + 1]);
    return 0;
  }
  if (!ts->test && !ts->ticking && pthread_create(&ts->ticker, NULL, ticker, rt) == 0)
    ts->ticking = true;
  oti_thread* t = calloc(1, sizeof(*t));
  if (!t) return oti_fail(rt, "out of host memory");
  t->object = t->entry = t->args = t->wake_value = t->kill_reason = t->interrupt = OT_NIL;
  t->watchers = OT_EMPTY;
  t->resume = R_BEGIN;
  if (!oti_native_stack_new(rt, &t->exec.native)) {
    free(t);
    return 0;
  }
  if (!oti_grow(rt, (void**)&ts->all, &ts->capacity, ts->count + 1, sizeof(oti_thread*))) {
    oti_native_stack_free(rt, &t->exec.native);
    free_thread(t);
    return 0;
  }
  t->state = T_WAITING;
  ts->all[ts->count++] = t;
  t->entry = rt->stack[first + 1];
  t->args = rt->stack[first + 2];
  ot_value object = oti_alloc(rt, K_THREAD, 3, OTI_BYTES(oti_thread_obj, 3));
  if (!object) {
    unregister(ts, t);
    oti_native_stack_free(rt, &t->exec.native);
    free_thread(t);
    return 0;
  }
  as_thread(object)->name = rt->stack[first];
  as_thread(object)->result = as_thread(object)->status = OT_NIL;
  as_thread(object)->thread = t;
  t->object = object;
  /* A fresh execution state with the parent's parameter bindings and
   * namespace, and nothing else. */
  oti_exec* x = &t->exec;
  x->native.callee = OT_NIL;
  x->restart_transfer[0] = x->restart_transfer[1] = OT_NIL;
  x->condition_values[0] = OT_NIL;
  x->current_namespace = rt->current_namespace;
  x->required_namespace = UINT32_MAX;
  x->source = OTI_NO_SOURCE;
  for (size_t i = 0; i < 4; i++) x->error.slots[i] = OT_NIL;
  /* As much room for arguments as every other thread has. */
  size_t args_capacity = rt->native.args_capacity;
  x->native.args = malloc(args_capacity * sizeof(ot_value));
  x->params = rt->param_count ? malloc(2 * rt->param_count * sizeof(ot_value)) : NULL;
  if (!x->native.args || (rt->param_count && !x->params)) {
    unregister(ts, t);
    free_exec(rt, x);
    free_thread(t);
    return oti_fail(rt, "out of host memory");
  }
  for (size_t i = 0; i < args_capacity; i++) x->native.args[i] = OT_NIL;
  x->native.args_capacity = args_capacity;
  if (rt->param_count) memcpy(x->params, rt->params, 2 * rt->param_count * sizeof(ot_value));
  x->param_count = x->param_capacity = rt->param_count;
  t->state = T_RUNNABLE;
  enqueue(&ts->run, t);
  return object;
}

static ot_value thread_value(ot_state* rt, ot_value v, const char* who) {
  if (oti_kind(v, K_THREAD)) return v;
  oti_type_error(rt, who, "a thread", v);
  return 0;
}

static ot_value self_builtin(ot_state* rt, size_t first, size_t count) {
  (void)first;
  if (count) return oti_fail(rt, "wrong number of arguments");
  if (!setup(rt)) return 0;
  return rt->threads->current->object;
}

static ot_value yield_builtin(ot_state* rt, size_t first, size_t count) {
  (void)first;
  if (count) return oti_fail(rt, "wrong number of arguments");
  oti_threads* ts = rt->threads;
  if (!ts || retried(rt, NULL)) return OT_NIL;
  if (!can_switch(rt) || (!ts->run.head && !ts->timer_count)) return OT_NIL;
  ts->current->state = T_RUNNABLE;
  enqueue(&ts->run, ts->current);
  return OTI_PARKED;
}

/* (%sleep deadline): park until then. Where a thread can't be switched,
 * such as while a module loads, the whole process waits. */
static ot_value sleep_builtin(ot_state* rt, size_t first, size_t count) {
  if (count != 1) return oti_fail(rt, "wrong number of arguments");
  uint64_t deadline = 0;
  if (!deadline_arg(rt, rt->stack[first], &deadline)) return 0;
  if (rt->threads) retried(rt, NULL);
  if (!deadline) return OT_NIL;
  if (!rt->threads || !can_switch(rt)) {
    for (uint64_t t = now(rt->threads); deadline > t; t = now(rt->threads)) {
      uint64_t step = deadline - t < 10000000u ? deadline - t : 10000000u;
      if (rt->threads && rt->threads->test) rt->threads->now += step;
      else pause_for(step);
      if (!ot_poll_interrupt(rt)) return 0;
    }
    return OT_NIL;
  }
  if (now(rt->threads) >= deadline) return OT_NIL;
  return wait_on(rt, NULL, deadline);
}

/* (%thread-join t deadline): #t once t has finished, :timeout when the
 * deadline passes first. */
static ot_value join_builtin(ot_state* rt, size_t first, size_t count) {
  if (count != 2) return oti_fail(rt, "wrong number of arguments");
  ot_value v = thread_value(rt, rt->stack[first], "thread/join");
  if (!v) return 0;
  uint64_t deadline = 0;
  if (!deadline_arg(rt, rt->stack[first + 1], &deadline)) return 0;
  if (rt->threads) retried(rt, NULL);
  oti_thread* t = as_thread(v)->thread;
  if (!t || !rt->threads) return OT_TRUE;
  if (t == rt->threads->current) return oti_fail(rt, "a thread cannot join itself");
  if (deadline && now(rt->threads) >= deadline) return keyword(rt, ":timeout");
  return wait_on(rt, &t->joiners, deadline);
}

static ot_value status_builtin(ot_state* rt, size_t first, size_t count) {
  if (count != 1) return oti_fail(rt, "wrong number of arguments");
  ot_value v = thread_value(rt, rt->stack[first], "thread/status");
  if (!v) return 0;
  oti_thread* t = as_thread(v)->thread;
  return t ? keyword(rt, state_name(t->state)) : as_thread(v)->status;
}

static ot_value result_builtin(ot_state* rt, size_t first, size_t count) {
  if (count != 1) return oti_fail(rt, "wrong number of arguments");
  ot_value v = thread_value(rt, rt->stack[first], "thread");
  return v ? as_thread(v)->result : 0;
}

static ot_value name_builtin(ot_state* rt, size_t first, size_t count) {
  if (count != 1) return oti_fail(rt, "wrong number of arguments");
  ot_value v = thread_value(rt, rt->stack[first], "thread");
  return v ? as_thread(v)->name : 0;
}

/* Live threads, as an array; with daemons? #f, those join-all waits for:
 * all but daemons and the caller. */
static ot_value threads_builtin(ot_state* rt, size_t first, size_t count) {
  if (count != 1) return oti_fail(rt, "wrong number of arguments");
  bool all = oti_truth(rt->stack[first]);
  if (!setup(rt)) return 0;
  oti_threads* ts = rt->threads;
  size_t base = rt->sp, n = 0;
  if (!oti_stack(rt, base + ts->count)) return 0;
  for (size_t i = 0; i < ts->count; i++)
    if (all || (!ts->all[i]->daemon && ts->all[i] != ts->current))
      rt->stack[base + n++] = ts->all[i]->object;
  rt->sp = base + n;
  ot_value result = oti_array_from(rt, base, n);
  rt->sp = base;
  return result;
}

static ot_value daemon_builtin(ot_state* rt, size_t first, size_t count) {
  if (count != 1) return oti_fail(rt, "wrong number of arguments");
  ot_value v = thread_value(rt, rt->stack[first], "thread/daemon!");
  if (!v) return 0;
  if (as_thread(v)->thread) as_thread(v)->thread->daemon = true;
  return v;
}

/* (%thread-kill t reason): make t unwind, running its cleanups; try can't
 * catch it. Killing yourself unwinds at once. */
static ot_value kill_builtin(ot_state* rt, size_t first, size_t count) {
  if (count != 2) return oti_fail(rt, "wrong number of arguments");
  ot_value v = thread_value(rt, rt->stack[first], "thread/kill!");
  if (!v) return 0;
  oti_thread* t = as_thread(v)->thread;
  if (!t || !rt->threads || t->kill_pending) return v;
  if (t == rt->threads->primordial) return oti_fail(rt, "the main thread cannot be killed");
  t->kill_reason = rt->stack[first + 1];
  if (t == rt->threads->current) {
    rt->interrupted = true;
    oti_error(rt, "thread killed");
    return 0;
  }
  t->kill_pending = true;
  wake(rt, t, OT_NIL);
  return v;
}

/* (%thread-interrupt t thunk): run thunk on t at its next safe point, or as
 * it wakes: a parked thread is woken for it. On the running thread it runs
 * now. */
static ot_value interrupt_builtin(ot_state* rt, size_t first, size_t count) {
  if (count != 2) return oti_fail(rt, "wrong number of arguments");
  ot_value v = thread_value(rt, rt->stack[first], "thread/interrupt!");
  if (!v) return 0;
  oti_thread* t = as_thread(v)->thread;
  if (!t || !rt->threads) return OT_FALSE;
  if (t == rt->threads->current) return oti_native_invoke(rt, rt->stack[first + 1], 0) ? OT_TRUE : 0;
  t->interrupt = rt->stack[first + 1];
  wake(rt, t, OT_NIL);
  return OT_TRUE;
}

/* (%monitor t ref): send the caller {:exit t :ref ref :reason r} when t ends,
 * at once if it has. */
static ot_value monitor_builtin(ot_state* rt, size_t first, size_t count) {
  if (count != 2) return oti_fail(rt, "wrong number of arguments");
  ot_value v = thread_value(rt, rt->stack[first], "actor/monitor");
  if (!v || !setup(rt)) return 0;
  ot_value done = keyword(rt, ":done");
  if (!done) return 0;
  bool normal = as_thread(rt->stack[first])->status == done;
  ot_value entry = oti_cons(rt, rt->threads->current->object, rt->stack[first + 1]);
  if (!entry) return 0;
  oti_thread* t = as_thread(rt->stack[first])->thread;
  ot_value list = oti_cons(rt, entry, t ? t->watchers : OT_EMPTY);
  if (!list) return 0;
  t = as_thread(rt->stack[first])->thread;
  if (t) {
    t->watchers = list;
    return OT_NIL;
  }
  /* Already finished: the message goes now. */
  notify_watchers(rt, rt->stack[first], list, normal);
  return OT_NIL;
}

/* (%demonitor t ref): stop the caller's monitor with ref. */
static ot_value demonitor_builtin(ot_state* rt, size_t first, size_t count) {
  if (count != 2) return oti_fail(rt, "wrong number of arguments");
  ot_value v = thread_value(rt, rt->stack[first], "actor/demonitor");
  if (!v) return 0;
  oti_thread* t = as_thread(v)->thread;
  if (!t) return OT_NIL;
  ot_value* link = &t->watchers;
  while (oti_kind(*link, K_PAIR)) {
    ot_value entry = oti_car(*link);
    if (oti_cdr(entry) == rt->stack[first + 1]) *link = oti_cdr(*link);
    else link = &oti_as_pair(*link)->cdr;
  }
  return OT_NIL;
}

/* --- Wait queues and atomic sections, for Otium's mutexes and condvars ---- */

static ot_value make_waitq_builtin(ot_state* rt, size_t first, size_t count) {
  (void)first;
  if (count) return oti_fail(rt, "wrong number of arguments");
  if (!setup(rt)) return 0;
  oti_threads* ts = rt->threads;
  oti_waitq* q = calloc(1, sizeof(*q));
  if (!q || !oti_grow(rt, (void**)&ts->queues, &ts->queue_capacity, ts->queue_count + 1,
                      sizeof(oti_waitq*))) {
    free(q);
    return oti_fail(rt, "out of host memory");
  }
  ts->queues[ts->queue_count++] = q;
  ot_value v = oti_alloc(rt, K_WAITQ, 0, OTI_BYTES(oti_waitq_obj, 0));
  if (!v) return 0;
  OTI_CAST(oti_waitq_obj, K_WAITQ, v)->queue = q;
  return v;
}

static oti_waitq* waitq_arg(ot_state* rt, ot_value v) {
  if (oti_kind(v, K_WAITQ)) return OTI_CAST(oti_waitq_obj, K_WAITQ, v)->queue;
  oti_type_error(rt, "wait queue", "a wait queue", v);
  return NULL;
}

/* (%park q deadline): wait on q, returning what woke it: the waker's value,
 * :timeout, or nil for an interrupt. An atomic section is released while
 * waiting and taken again after, which makes check-then-park safe. */
static ot_value park_builtin(ot_state* rt, size_t first, size_t count) {
  if (count != 2) return oti_fail(rt, "wrong number of arguments");
  if (!setup(rt)) return 0;
  oti_waitq* q = waitq_arg(rt, rt->stack[first]);
  uint64_t deadline = 0;
  if (!q || !deadline_arg(rt, rt->stack[first + 1], &deadline)) return 0;
  oti_thread* me = rt->threads->current;
  ot_value woke;
  if (retried(rt, &woke)) {
    rt->atomic = me->saved_atomic;
    return woke;
  }
  ot_value result = wait_on(rt, q, deadline);
  if (result == OTI_PARKED) {
    me->saved_atomic = rt->atomic;
    rt->atomic = 0;
  }
  return result;
}

static ot_value wake_one_builtin(ot_state* rt, size_t first, size_t count) {
  if (count != 1) return oti_fail(rt, "wrong number of arguments");
  oti_waitq* q = waitq_arg(rt, rt->stack[first]);
  if (!q) return 0;
  if (!q->head) return OT_FALSE;
  wake(rt, q->head, OT_TRUE);
  return OT_TRUE;
}

static ot_value wake_all_builtin(ot_state* rt, size_t first, size_t count) {
  if (count != 1) return oti_fail(rt, "wrong number of arguments");
  oti_waitq* q = waitq_arg(rt, rt->stack[first]);
  if (!q) return 0;
  while (q->head) wake(rt, q->head, OT_TRUE);
  return OT_NIL;
}

/* (%atomic #t) enters a section no other thread preempts; (%atomic #f)
 * leaves it. */
static ot_value atomic_builtin(ot_state* rt, size_t first, size_t count) {
  if (count != 1) return oti_fail(rt, "wrong number of arguments");
  if (rt->stack[first] == OT_TRUE) rt->atomic++;
  else if (rt->atomic) rt->atomic--;
  return OT_NIL;
}

/* --- Mailboxes ---------------------------------------------------------- */

/* (%send t message): append to t's mailbox; #f when t has finished. */
static ot_value send_builtin(ot_state* rt, size_t first, size_t count) {
  if (count != 2) return oti_fail(rt, "wrong number of arguments");
  ot_value v = thread_value(rt, rt->stack[first], "send");
  if (!v) return 0;
  oti_thread* t = as_thread(v)->thread;
  if (!t) return OT_FALSE;
  if (!oti_grow(rt, (void**)&t->mailbox, &t->mailbox_capacity, t->mailbox_count + 1,
                sizeof(ot_value)))
    return 0;
  t->mailbox[t->mailbox_count++] = rt->stack[first + 1];
  if (t->mail_waiting) wake(rt, t, OT_TRUE);
  return OT_TRUE;
}

/* (%mailbox-next deadline): the next message after the cursor, waiting for
 * one until the deadline from %thread-deadline; :timeout when it passes.
 * One receive has one deadline however many messages it skips. */
static ot_value mailbox_next_builtin(ot_state* rt, size_t first, size_t count) {
  if (count != 1) return oti_fail(rt, "wrong number of arguments");
  if (!setup(rt)) return 0;
  uint64_t deadline = 0;
  if (!deadline_arg(rt, rt->stack[first], &deadline)) return 0;
  oti_threads* ts = rt->threads;
  oti_thread* me = ts->current;
  retried(rt, NULL);
  if (me->cursor < me->mailbox_count) return me->mailbox[me->cursor++];
  if (deadline && now(ts) >= deadline) return keyword(rt, ":timeout");
  ot_value result = wait_on(rt, NULL, deadline);
  if (result == OTI_PARKED) me->mail_waiting = true;
  return result;
}

/* Remove the message the last %mailbox-next returned, so the next scan
 * starts from the beginning. */
static ot_value mailbox_take_builtin(ot_state* rt, size_t first, size_t count) {
  (void)first;
  if (count) return oti_fail(rt, "wrong number of arguments");
  if (!setup(rt)) return 0;
  oti_thread* me = rt->threads->current;
  if (me->cursor) {
    size_t at = me->cursor - 1;
    memmove(me->mailbox + at, me->mailbox + at + 1, (me->mailbox_count - at - 1) * sizeof(ot_value));
    me->mailbox_count--;
  }
  me->cursor = 0;
  return OT_NIL;
}

static ot_value mailbox_rewind_builtin(ot_state* rt, size_t first, size_t count) {
  (void)first;
  if (count) return oti_fail(rt, "wrong number of arguments");
  if (!setup(rt)) return 0;
  rt->threads->current->cursor = 0;
  return OT_NIL;
}

/* (%thread-deadline seconds): an absolute deadline for the waiting
 * builtins, or nil. Virtual under the test scheduler, so it sets that up. */
static ot_value deadline_builtin(ot_state* rt, size_t first, size_t count) {
  if (count != 1) return oti_fail(rt, "wrong number of arguments");
  if (!setup(rt)) return 0;
  uint64_t deadline = 0;
  if (!deadline_after(rt, rt->stack[first], &deadline)) return 0;
  return deadline ? ot_int((intptr_t)deadline) : OT_NIL;
}

/* Seconds on the scheduler's clock, virtual under the test scheduler. */
static ot_value clock_builtin(ot_state* rt, size_t first, size_t count) {
  (void)first;
  if (count) return oti_fail(rt, "wrong number of arguments");
  return oti_float(rt, (double)now(rt->threads) / 1e9);
}

#define THREAD_BUILTIN(id, name, fn) {id, name, fn},
#else
#define THREAD_BUILTIN(id, name, fn) {id, name, no_threads},

void oti_visit_threads(ot_state* rt, oti_root_visitor visit, void* context) {
  (void)rt;
  (void)visit;
  (void)context;
}

void oti_threads_destroy(ot_state* rt) { (void)rt; }

bool oti_threads_grow_args(ot_state* rt) {
  (void)rt;
  return true;
}

size_t oti_thread_count(ot_state* rt) {
  (void)rt;
  return 1;
}

bool oti_thread_foreground(ot_state* rt) {
  (void)rt;
  return true;
}

static ot_value no_threads(ot_state* rt, size_t first, size_t count) {
  (void)first;
  (void)count;
  return oti_fail(rt, "threads need native code, which this build lacks");
}
#endif

const oti_builtin_def oti_thread_builtins[] = {
    THREAD_BUILTIN(B_SPAWN, "%spawn", spawn_builtin)
    THREAD_BUILTIN(B_SELF, "%self", self_builtin)
    THREAD_BUILTIN(B_YIELD, "%yield", yield_builtin)
    THREAD_BUILTIN(B_SLEEP, "%sleep", sleep_builtin)
    THREAD_BUILTIN(B_THREAD_JOIN, "%thread-join", join_builtin)
    THREAD_BUILTIN(B_THREAD_STATUS, "%thread-status", status_builtin)
    THREAD_BUILTIN(B_THREAD_RESULT, "%thread-result", result_builtin)
    THREAD_BUILTIN(B_THREAD_NAME, "%thread-name", name_builtin)
    THREAD_BUILTIN(B_THREADS, "%threads", threads_builtin)
    THREAD_BUILTIN(B_THREAD_DAEMON, "%thread-daemon", daemon_builtin)
    THREAD_BUILTIN(B_THREAD_KILL, "%thread-kill", kill_builtin)
    THREAD_BUILTIN(B_MAKE_WAITQ, "%make-waitq", make_waitq_builtin)
    THREAD_BUILTIN(B_PARK, "%park", park_builtin)
    THREAD_BUILTIN(B_WAKE_ONE, "%wake-one", wake_one_builtin)
    THREAD_BUILTIN(B_WAKE_ALL, "%wake-all", wake_all_builtin)
    THREAD_BUILTIN(B_ATOMIC, "%atomic", atomic_builtin)
    THREAD_BUILTIN(B_SEND, "%send", send_builtin)
    THREAD_BUILTIN(B_MAILBOX_NEXT, "%mailbox-next", mailbox_next_builtin)
    THREAD_BUILTIN(B_MAILBOX_TAKE, "%mailbox-take", mailbox_take_builtin)
    THREAD_BUILTIN(B_MAILBOX_REWIND, "%mailbox-rewind", mailbox_rewind_builtin)
    THREAD_BUILTIN(B_THREAD_DEADLINE, "%thread-deadline", deadline_builtin)
    THREAD_BUILTIN(B_SCHEDULER_CLOCK, "%scheduler-clock", clock_builtin)
    THREAD_BUILTIN(B_THREAD_INTERRUPT, "%thread-interrupt", interrupt_builtin)
    THREAD_BUILTIN(B_MONITOR, "%monitor", monitor_builtin)
    THREAD_BUILTIN(B_DEMONITOR, "%demonitor", demonitor_builtin)
    {0, NULL, NULL},
};
