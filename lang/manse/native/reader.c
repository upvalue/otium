// PTY output read on a thread of its own, so a child never waits for a frame.
//
// A PTY holds about 1 KiB on macOS. Read once per frame, a child printing a
// lot spends most of its time blocked. This follows the read thread in
// Ghostty's src/termio/Exec.zig: a gather thread reads into a ring of fixed
// batches, and the Otium thread parses whole batches between frames. The
// thread never enters Otium; it only fills batches and pushes an SDL event to
// end the UI loop's wait when output arrives.

#include <SDL3/SDL.h>
#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <pthread.h>
#include <signal.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <time.h>
#include <unistd.h>

enum {
  BATCHES = 4,
  BATCH_CAPACITY = 64 * 1024,
  // Less than this is handed over at once, so typing and prompts stay prompt.
  BRIDGE_THRESHOLD = 1024,
  BRIDGE_POLL_MS = 1,
};

// How long a busy stream may keep filling one batch.
static const int64_t GATHER_BUDGET_NS = 3 * 1000 * 1000;

typedef struct {
  int fd;
  int quit[2];
  pthread_t thread;
  pthread_mutex_t mutex;
  pthread_cond_t slot_free;
  uint8_t* batches;
  size_t lens[BATCHES];
  // Batches published and consumed; head - tail is the number ready.
  unsigned head, tail;
  bool stopping, ended, woken;
  // The errno that ended the stream, or 0 for an ordinary end.
  int error;
  Uint32 wake_event;
} manse_reader;

static int64_t now_ns(void) {
  struct timespec t;
  clock_gettime(CLOCK_MONOTONIC, &t);
  return (int64_t)t.tv_sec * 1000000000 + t.tv_nsec;
}

static void push_wake(manse_reader* r) {
  if (!r->wake_event) return;
  SDL_Event event;
  SDL_zero(event);
  event.type = r->wake_event;
  SDL_PushEvent(&event);
}

// 1 when the PTY is readable or hung up, 0 on timeout, -1 when asked to quit.
static int wait_readable(manse_reader* r, int timeout_ms) {
  struct pollfd fds[2] = {{.fd = r->fd, .events = POLLIN}, {.fd = r->quit[0], .events = POLLIN}};
  for (;;) {
    int n = poll(fds, 2, timeout_ms);
    if (n < 0 && errno == EINTR) continue;
    if (n < 0) return 1;  // Let read report the error.
    if (fds[1].revents) return -1;
    return n > 0;
  }
}

// Fill one batch. Returns its length, or -1 when asked to quit. Sets *error
// to an errno, or 0, when the stream ends; leaves it at -1 otherwise.
static long gather(manse_reader* r, uint8_t* batch, int* error) {
  size_t total = 0;
  int64_t deadline = 0;
  for (;;) {
    int timeout = -1;
    if (total > 0 && total < BRIDGE_THRESHOLD) {
      timeout = 0;
    } else if (total > 0) {
      int64_t now = now_ns();
      if (!deadline) deadline = now + GATHER_BUDGET_NS;
      if (now >= deadline) break;
      timeout = BRIDGE_POLL_MS;
    }
    int ready = wait_readable(r, timeout);
    if (ready < 0) return -1;
    if (ready == 0) break;
    ssize_t n = read(r->fd, batch + total, BATCH_CAPACITY - total);
    if (n < 0 && (errno == EINTR || errno == EAGAIN)) continue;
    if (n <= 0) {
      // macOS reports EIO once the child side has closed.
      *error = n < 0 && errno != EIO ? errno : 0;
      break;
    }
    total += (size_t)n;
    if (total == BATCH_CAPACITY) break;
  }
  return (long)total;
}

static void* run(void* arg) {
  manse_reader* r = arg;
  for (;;) {
    pthread_mutex_lock(&r->mutex);
    while (r->head - r->tail == BATCHES && !r->stopping) pthread_cond_wait(&r->slot_free, &r->mutex);
    bool stopping = r->stopping;
    unsigned slot = r->head % BATCHES;
    pthread_mutex_unlock(&r->mutex);
    if (stopping) break;

    int error = -1;
    long n = gather(r, r->batches + (size_t)slot * BATCH_CAPACITY, &error);
    if (n < 0) break;

    pthread_mutex_lock(&r->mutex);
    if (n > 0) {
      r->lens[slot] = (size_t)n;
      r->head++;
    }
    if (error >= 0) {
      r->ended = true;
      r->error = error;
    }
    bool wake = !r->woken && (n > 0 || error >= 0);
    if (wake) r->woken = true;
    pthread_mutex_unlock(&r->mutex);
    if (wake) push_wake(r);
    if (error >= 0) break;
  }
  return NULL;
}

static void release(manse_reader* r) {
  if (r->quit[0] >= 0) close(r->quit[0]);
  if (r->quit[1] >= 0) close(r->quit[1]);
  free(r->batches);
  free(r);
}

// Start reading fd. Returns NULL with errno set on failure.
manse_reader* manse_reader_start(int fd) {
  static SDL_AtomicU32 wake_event;
  manse_reader* r = calloc(1, sizeof *r);
  if (!r) return NULL;
  r->fd = fd;
  r->quit[0] = r->quit[1] = -1;
  r->batches = malloc((size_t)BATCHES * BATCH_CAPACITY);
  if (!r->batches || pipe(r->quit) < 0) {
    int saved = errno;
    release(r);
    errno = saved;
    return NULL;
  }
  for (int i = 0; i < 2; i++) fcntl(r->quit[i], F_SETFD, FD_CLOEXEC);

  // One SDL event type serves every reader. It has no window, so the UI
  // drops it after it has ended the wait.
  Uint32 type = SDL_GetAtomicU32(&wake_event);
  if (!type) {
    type = SDL_RegisterEvents(1);
    if (!SDL_CompareAndSwapAtomicU32(&wake_event, 0, type)) type = SDL_GetAtomicU32(&wake_event);
  }
  r->wake_event = type;

  pthread_mutex_init(&r->mutex, NULL);
  pthread_cond_init(&r->slot_free, NULL);
  // Block signals on the new thread so they reach the Otium thread and its
  // handlers, never a thread the runtime does not know.
  sigset_t all, saved;
  sigfillset(&all);
  pthread_sigmask(SIG_SETMASK, &all, &saved);
  int result = pthread_create(&r->thread, NULL, run, r);
  pthread_sigmask(SIG_SETMASK, &saved, NULL);
  if (result != 0) {
    pthread_cond_destroy(&r->slot_free);
    pthread_mutex_destroy(&r->mutex);
    release(r);
    errno = result;
    return NULL;
  }
  return r;
}

// The oldest unconsumed batch: its length, with its address in *data. 0 when
// none is ready, -1 when the stream has ended and every batch is consumed.
long manse_reader_peek(manse_reader* r, uint8_t** data) {
  long n;
  pthread_mutex_lock(&r->mutex);
  if (r->head != r->tail) {
    unsigned slot = r->tail % BATCHES;
    *data = r->batches + (size_t)slot * BATCH_CAPACITY;
    n = (long)r->lens[slot];
  } else {
    // Empty: the next batch must wake the loop again.
    r->woken = false;
    n = r->ended ? -1 : 0;
  }
  pthread_mutex_unlock(&r->mutex);
  return n;
}

// Release the batch peek returned, for the thread to fill again.
void manse_reader_consume(manse_reader* r) {
  pthread_mutex_lock(&r->mutex);
  if (r->head != r->tail) {
    r->tail++;
    pthread_cond_signal(&r->slot_free);
  }
  pthread_mutex_unlock(&r->mutex);
}

// The errno that ended the stream, or 0.
int manse_reader_error(manse_reader* r) {
  pthread_mutex_lock(&r->mutex);
  int error = r->error;
  pthread_mutex_unlock(&r->mutex);
  return error;
}

// Wake the UI loop again, for a caller that leaves batches unconsumed.
void manse_reader_wake(manse_reader* r) {
  push_wake(r);
}

// Stop the thread and free the reader. Call before closing the fd.
void manse_reader_stop(manse_reader* r) {
  if (!r) return;
  pthread_mutex_lock(&r->mutex);
  r->stopping = true;
  pthread_cond_signal(&r->slot_free);
  pthread_mutex_unlock(&r->mutex);
  ssize_t ignored = write(r->quit[1], "x", 1);
  (void)ignored;
  pthread_join(r->thread, NULL);
  pthread_cond_destroy(&r->slot_free);
  pthread_mutex_destroy(&r->mutex);
  release(r);
}
