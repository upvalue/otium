#include "internal.h"

/* Green threads: many threads of Otium code on one native thread. See
 * scratch/green-threads.md.
 *
 * In native processes each thread has a machine stack of its own and a saved
 * copy of rt->exec, the per-thread part of the state. A switch copies the
 * running thread's exec out and the next one's in, then swaps stack
 * pointers. C frames stay on the stack they were made on, so a thread can
 * be switched out wherever C code calls the scheduler: when it parks, yields
 * or is preempted at a poll.
 *
 * The scheduler is a FIFO run queue, a list of timers sorted by deadline and
 * wait queues. A parked thread is on at most one wait queue and the timer
 * list; waking takes it off both before it is queued to run, so a timeout
 * and a wake can't both happen. Preemption is a ticker that trips the
 * native stack limit every millisecond while other threads are runnable or
 * sleeping; the poll it trips yields. With OTIUM_SCHED=seed[:slice] the scheduler is
 * deterministic instead, for tests: every poll counts towards a slice of
 * that many polls (random from the seed when not given), the next thread is
 * chosen at random, and time is virtual, jumping to the next deadline when
 * nothing can run. */

#ifdef OTIUM_NATIVE

#include <errno.h>
#include <pthread.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <time.h>
#include <unistd.h>

typedef enum { T_RUNNABLE, T_RUNNING, T_WAITING, T_DONE, T_FAILED, T_KILLED } oti_thread_state;

typedef struct oti_thread oti_thread;

/* A wait queue: threads parked until something wakes them. */
typedef struct {
  oti_thread *head, *tail;
} oti_waitq;

struct oti_thread {
  /* Saved while another thread runs. */
  oti_exec exec;
  uintptr_t machine_sp;
  /* The head of its root list while it isn't running. */
  oti_roots* roots;
  /* The machine stack; NULL for the primordial thread, which runs on the
   * process's. */
  unsigned char* stack;
  size_t stack_bytes;
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
  bool timed, daemon, kill_pending, mail_waiting;
  ot_value* mailbox;
  size_t mailbox_count, mailbox_capacity, cursor;
  oti_waitq joiners;
};

struct oti_threads {
  /* Live threads, including the running one. */
  oti_thread** all;
  size_t count, capacity;
  oti_thread *current, *primordial;
  /* A finished thread whose stack is freed once another runs. */
  oti_thread* dead;
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

/* Reserved, and committed as it is touched. */
#define THREAD_STACK ((size_t)64 << 20)
/* Room below the limit for C helpers. */
#define THREAD_MARGIN ((size_t)1 << 20)

/* oti_thread_switch(save, to): save the callee-saved registers and stack
 * pointer, load another stack and return on it. A new thread's stack starts
 * as if switched out just before oti_thread_start, with the state in rbx. */
__asm__(".text\n"
        ".p2align 4\n"
        ".globl oti_thread_switch\n"
        ".type oti_thread_switch,@function\n"
        "oti_thread_switch:\n"
        "  push %rbp\n"
        "  push %rbx\n"
        "  push %r12\n"
        "  push %r13\n"
        "  push %r14\n"
        "  push %r15\n"
        "  mov %rsp, (%rdi)\n"
        "  mov %rsi, %rsp\n"
        "  pop %r15\n"
        "  pop %r14\n"
        "  pop %r13\n"
        "  pop %r12\n"
        "  pop %rbx\n"
        "  pop %rbp\n"
        "  ret\n"
        ".size oti_thread_switch, .-oti_thread_switch\n"
        ".p2align 4\n"
        ".globl oti_thread_start\n"
        ".type oti_thread_start,@function\n"
        "oti_thread_start:\n"
        "  and $-16, %rsp\n"
        "  mov %rbx, %rdi\n"
        "  call oti_thread_main\n"
        "  ud2\n"
        ".size oti_thread_start, .-oti_thread_start\n");

void oti_thread_switch(uintptr_t* save, uintptr_t to);
extern char oti_thread_start[];
_Noreturn void oti_thread_main(ot_state* rt);

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

/* Make a parked thread runnable, its park returning value. */
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
  if (t == rt->threads->current || t->state >= T_DONE) return;
  oti_visit_exec(rt, &t->exec, visit, context);
  /* Roots it pushed; the shared ones below are visited with the running
   * thread's. */
  for (oti_roots* r = t->roots; r && r != rt->base_roots; r = r->previous)
    for (size_t i = 0; i < r->count; i++) visit(context, &r->slots[i]);
}

void oti_visit_threads(ot_state* rt, oti_root_visitor visit, void* context) {
  oti_threads* ts = rt->threads;
  if (!ts) return;
  for (size_t i = 0; i < ts->count; i++) visit_thread(rt, ts->all[i], visit, context);
  if (ts->dead) visit_thread(rt, ts->dead, visit, context);
}

static void free_exec(oti_exec* x) {
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
  oti_release_error(&x->error);
}

static void free_thread(oti_thread* t) {
  if (t->stack) {
    /* ASan marks C frames' redzones and clears them as the frames return.
     * Frames still on a finished thread's stack never return, so their
     * marks would outlive the mapping and trip whatever is mapped there
     * next. */
    OTI_UNPOISON(t->stack, t->stack_bytes);
    munmap(t->stack, t->stack_bytes);
  }
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
 * program without threads has none of this. */
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
  ot_value object = oti_alloc(rt, K_THREAD, 3, OTI_BYTES(oti_thread_obj, 3));
  if (!object) return false;
  as_thread(object)->name = OT_NIL;
  as_thread(object)->result = as_thread(object)->status = OT_NIL;
  as_thread(object)->thread = primordial;
  primordial->object = object;
  ot_value name = keyword(rt, ":main");
  if (!name) return false;
  as_thread(primordial->object)->name = name;
  const char* sched = getenv("OTIUM_SCHED");
  if (sched && *sched) {
    ts->test = true;
    char* end;
    ts->seed = strtoull(sched, &end, 10) * 2654435761u + 88172645463325252u;
    ts->slice = *end == ':' ? strtoull(end + 1, NULL, 10) : 0;
    ts->now = 1;
    new_slice(ts);
  } else if (pthread_create(&ts->ticker, NULL, ticker, rt) == 0)
    ts->ticking = true;
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
    if (t != ts->current) free_exec(&t->exec);
    free_thread(t);
  }
  if (ts->dead) free_thread(ts->dead);
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

static void switch_to(ot_state* rt, oti_thread* next) {
  oti_threads* ts = rt->threads;
  oti_thread* me = ts->current;
  next->state = T_RUNNING;
  if (next == me) return;
  if (me->state < T_DONE) me->exec = rt->exec;
  me->roots = rt->heap.roots;
  rt->exec = next->exec;
  rt->heap.roots = next->roots;
  ts->current = next;
  oti_thread_switch(&me->machine_sp, next->machine_sp);
  /* Running again: whoever switched here loaded this thread's exec. A thread
   * that finished meanwhile can now lose its stack. */
  if (ts->dead) {
    free_thread(ts->dead);
    ts->dead = NULL;
  }
}

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

/* Run something else: the current thread has been queued to run again,
 * parked, or finished. Sleeps when only timers can make progress. Returns
 * once this thread runs again. With nothing runnable and nothing timed, the
 * primordial thread, which is then waiting, wakes with :deadlock. */
static void reschedule(ot_state* rt) {
  oti_threads* ts = rt->threads;
  for (;;) {
    expire(rt);
    oti_thread* next = pick(ts);
    if (next) {
      ts->preempt = 0;
      new_slice(ts);
      switch_to(rt, next);
      return;
    }
    if (ts->timer_count) {
      uint64_t deadline = ts->timers[0]->deadline, t = now(ts);
      if (ts->test) ts->now = deadline;
      else if (deadline > t) pause_for(deadline - t);
      continue;
    }
    oti_thread* waiting = ts->primordial->state == T_WAITING ? ts->primordial : ts->current;
    if (waiting->state != T_WAITING) abort();
    wake(rt, waiting, keyword(rt, ":deadlock"));
  }
}

/* What a thread checks when it runs again after parking or yielding. False
 * when it must unwind: it was killed, or another thread asked the process
 * to exit and this is the primordial thread. */
static bool resumed(ot_state* rt) {
  oti_threads* ts = rt->threads;
  oti_thread* me = ts->current;
  /* ^C, which oti_thread_foreground sent here. */
  if (me == ts->primordial && !oti_interrupt_point(rt)) return false;
  /* A thunk from thread/interrupt! runs here, on top of whatever this thread
   * was doing, unless it is in an atomic section. */
  if (me->interrupt != OT_NIL && !rt->atomic && !me->kill_pending) {
    ot_value thunk = me->interrupt;
    me->interrupt = OT_NIL;
    if (!oti_native_invoke(rt, thunk, 0)) return false;
  }
  if (me->kill_pending && !rt->interrupted) {
    me->kill_pending = false;
    rt->interrupted = true;
    oti_error(rt, "thread killed");
    return false;
  }
  if (rt->exit_pending && me == ts->primordial) {
    if (!oti_failed(rt)) oti_error(rt, "exit requested");
    return false;
  }
  return true;
}

static bool loading_module(ot_state* rt) { return oti_module_depth(rt) > 0; }

/* Park the current thread until woken, on q and until deadline when given.
 * Returns the value the waker gave, :timeout or :deadlock; 0 after an error,
 * when the thread must unwind. */
static ot_value park(ot_state* rt, oti_waitq* q, uint64_t deadline) {
  oti_threads* ts = rt->threads;
  oti_thread* me = ts->current;
  if (q && loading_module(rt)) {
    oti_coded_error(rt, "E0502", "cannot wait for another thread while loading a module");
    return 0;
  }
  if (me->kill_pending) return resumed(rt) ? OT_NIL : 0;
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
  reschedule(rt);
  if (!resumed(rt)) return 0;
  if (me->wake_value == keyword(rt, ":deadlock")) {
    oti_coded_error(rt, "E0503", "deadlock: no thread can run and none is sleeping");
    return 0;
  }
  return me->wake_value;
}

/* Let the others run, the current thread going to the back of the queue. */
static bool yield(ot_state* rt) {
  oti_threads* ts = rt->threads;
  oti_thread* me = ts->current;
  if (ts->run.head || ts->timer_count) {
    me->state = T_RUNNABLE;
    enqueue(&ts->run, me);
    reschedule(rt);
  }
  return resumed(rt);
}

/* An interrupt such as ^C is for the main thread. Another thread that finds
 * one pending wakes the main thread, if it is waiting, and yields to it, and
 * the main thread takes the interrupt as it resumes. True when the running
 * thread is the main one. */
bool oti_thread_foreground(ot_state* rt) {
  oti_threads* ts = rt->threads;
  if (ts->current == ts->primordial) return true;
  wake(rt, ts->primordial, OT_NIL);
  ts->preempt = 1;
  return false;
}

/* The poll found its limit tripped: yield when the ticker or the test
 * scheduler's slice says so. False when the thread must unwind. */
bool oti_thread_poll(ot_state* rt) {
  oti_threads* ts = rt->threads;
  if (ts->test) {
    /* Every poll counts; keep the limit tripped so the next one does too. */
    rt->native.limit = UINTPTR_MAX;
    if (ts->budget && --ts->budget) return resumed(rt);
    ts->preempt = 1;
  }
  if (!ts->preempt || rt->atomic || loading_module(rt)) return resumed(rt);
  ts->preempt = 0;
  return yield(rt);
}

/* --- Starting and finishing threads -------------------------------------- */

static void notify_watchers(ot_state* rt, ot_value object, ot_value watchers, bool normal);

_Noreturn void oti_thread_main(ot_state* rt) {
  oti_threads* ts = rt->threads;
  oti_thread* me = ts->current;
  if (ts->test) rt->native.limit = UINTPTR_MAX;
  size_t argc = 0;
  for (ot_value p = me->args; oti_kind(p, K_PAIR); p = oti_cdr(p)) argc++;
  ot_value result = 0;
  if (oti_native_args(rt, argc)) {
    size_t i = 0;
    for (ot_value p = me->args; oti_kind(p, K_PAIR); p = oti_cdr(p)) rt->native.args[i++] = oti_car(p);
    result = oti_native_invoke(rt, me->entry, argc);
  }
  me->entry = me->args = OT_NIL;
  /* The thread stays running until it is done: reporting a failure runs
   * Otium code, which can yield. */
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
  me->state = end;
  ot_value status = keyword(rt, state_name(me->state));
  as_thread(me->object)->status = status;
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
  free_exec(&rt->exec);
  memset(&rt->exec, 0, sizeof(rt->exec));
  ts->dead = me;
  reschedule(rt);
  abort();
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
  size_t page = (size_t)sysconf(_SC_PAGESIZE);
  oti_thread* t = calloc(1, sizeof(*t));
  if (!t) return oti_fail(rt, "out of host memory");
  t->object = t->entry = t->args = t->wake_value = t->kill_reason = t->interrupt = OT_NIL;
  t->watchers = OT_EMPTY;
  t->roots = rt->base_roots;
  t->stack_bytes = THREAD_STACK;
  void* stack = mmap(NULL, t->stack_bytes, PROT_READ | PROT_WRITE,
                     MAP_PRIVATE | MAP_ANONYMOUS | MAP_NORESERVE, -1, 0);
  if (stack == MAP_FAILED) {
    free(t);
    return oti_fail(rt, "cannot reserve a thread stack");
  }
  t->stack = stack;
  /* A guard page catches C helpers that run past the margin. */
  mprotect(stack, page, PROT_NONE);
  if (!oti_grow(rt, (void**)&ts->all, &ts->capacity, ts->count + 1, sizeof(oti_thread*))) {
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
  x->native.base_limit = x->native.limit = (uintptr_t)stack + page + THREAD_MARGIN;
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
    free_exec(x);
    free_thread(t);
    return oti_fail(rt, "out of host memory");
  }
  for (size_t i = 0; i < args_capacity; i++) x->native.args[i] = OT_NIL;
  x->native.args_capacity = args_capacity;
  if (rt->param_count) memcpy(x->params, rt->params, 2 * rt->param_count * sizeof(ot_value));
  x->param_count = x->param_capacity = rt->param_count;
  /* The stack as oti_thread_switch leaves a thread it switched out: six
   * callee-saved registers, rbx holding the state, then oti_thread_start. */
  uintptr_t* top = (uintptr_t*)(((uintptr_t)stack + t->stack_bytes) & ~(uintptr_t)15);
  top[-1] = 0;
  top[-2] = (uintptr_t)oti_thread_start;
  top[-3] = 0;             /* rbp */
  top[-4] = (uintptr_t)rt; /* rbx */
  top[-5] = top[-6] = top[-7] = top[-8] = 0;
  t->machine_sp = (uintptr_t)(top - 8);
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
  if (!rt->threads) return OT_NIL;
  return yield(rt) ? OT_NIL : 0;
}

/* (%sleep seconds): park until then. While a module loads nothing else may
 * run, so the whole process waits. */
static ot_value sleep_builtin(ot_state* rt, size_t first, size_t count) {
  if (count != 1) return oti_fail(rt, "wrong number of arguments");
  uint64_t deadline;
  if (!deadline_after(rt, rt->stack[first], &deadline)) return 0;
  if (!deadline) return OT_NIL;
  if (!rt->threads || loading_module(rt)) {
    for (uint64_t t = now(rt->threads); deadline > t; t = now(rt->threads)) {
      uint64_t step = deadline - t < 10000000u ? deadline - t : 10000000u;
      if (rt->threads && rt->threads->test) rt->threads->now += step;
      else pause_for(step);
      if (!ot_poll_interrupt(rt)) return 0;
    }
    return OT_NIL;
  }
  /* Woken early, to run an interrupt thunk, it sleeps on. */
  for (;;) {
    ot_value woke = park(rt, NULL, deadline);
    if (!woke) return 0;
    if (woke == keyword(rt, ":timeout") || now(rt->threads) >= deadline) return OT_NIL;
  }
}

/* (%thread-join t timeout): wait for t to finish: #t when it has, :timeout
 * when not. */
static ot_value join_builtin(ot_state* rt, size_t first, size_t count) {
  if (count != 2) return oti_fail(rt, "wrong number of arguments");
  ot_value v = thread_value(rt, rt->stack[first], "thread/join");
  if (!v) return 0;
  oti_thread* t = as_thread(v)->thread;
  if (!t || !rt->threads) return OT_TRUE;
  if (t == rt->threads->current) return oti_fail(rt, "a thread cannot join itself");
  uint64_t deadline;
  if (!deadline_after(rt, rt->stack[first + 1], &deadline)) return 0;
  /* Woken early, to run an interrupt thunk, it waits on. */
  for (;;) {
    ot_value woke = park(rt, &t->joiners, deadline);
    if (!woke || woke == keyword(rt, ":timeout")) return woke;
    t = as_thread(rt->stack[first])->thread;
    if (!t) return OT_TRUE;
  }
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
  t->kill_pending = true;
  if (t == rt->threads->current) return resumed(rt) ? v : 0;
  wake(rt, t, OT_NIL);
  return v;
}

/* (%thread-interrupt t thunk): run thunk on t at its next safe point, or as
 * it wakes: a parked thread is woken for it. */
static ot_value interrupt_builtin(ot_state* rt, size_t first, size_t count) {
  if (count != 2) return oti_fail(rt, "wrong number of arguments");
  ot_value v = thread_value(rt, rt->stack[first], "thread/interrupt!");
  if (!v) return 0;
  oti_thread* t = as_thread(v)->thread;
  if (!t || !rt->threads) return OT_FALSE;
  t->interrupt = rt->stack[first + 1];
  if (t == rt->threads->current) return resumed(rt) ? OT_TRUE : 0;
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

/* (%park q timeout): wait on q. An atomic section is released while
 * waiting and taken again after, which makes check-then-park safe. */
static ot_value park_builtin(ot_state* rt, size_t first, size_t count) {
  if (count != 2) return oti_fail(rt, "wrong number of arguments");
  if (!setup(rt)) return 0;
  oti_waitq* q = waitq_arg(rt, rt->stack[first]);
  uint64_t deadline;
  if (!q || !deadline_after(rt, rt->stack[first + 1], &deadline)) return 0;
  size_t atomic = rt->atomic;
  rt->atomic = 0;
  ot_value result = park(rt, q, deadline);
  rt->atomic = atomic;
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
 * one until the deadline from %thread-deadline; :timeout when it passes. One
 * receive has one deadline however many messages it skips. */
static ot_value mailbox_next_builtin(ot_state* rt, size_t first, size_t count) {
  if (count != 1) return oti_fail(rt, "wrong number of arguments");
  if (!setup(rt)) return 0;
  oti_threads* ts = rt->threads;
  oti_thread* me = ts->current;
  uint64_t deadline = ot_is_int(rt->stack[first]) ? (uint64_t)ot_to_int(rt->stack[first]) : 0;
  for (;;) {
    if (me->cursor < me->mailbox_count) return me->mailbox[me->cursor++];
    if (deadline && now(ts) >= deadline) return keyword(rt, ":timeout");
    me->mail_waiting = true;
    ot_value woke = park(rt, NULL, deadline);
    me->mail_waiting = false;
    if (!woke) return 0;
  }
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

/* (%thread-deadline seconds): an absolute deadline for %mailbox-next, or nil. */
static ot_value deadline_builtin(ot_state* rt, size_t first, size_t count) {
  if (count != 1) return oti_fail(rt, "wrong number of arguments");
  if (!setup(rt)) return 0;
  uint64_t deadline;
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
