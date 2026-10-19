#define _POSIX_C_SOURCE 200809L
#include "transport.h"
#include "../support/utf8.h"
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/un.h>
#include <poll.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>
#include <limits.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#define LIMIT (4u * 1024u * 1024u)
typedef struct client {
  int fd, id;
  unsigned char *input, *output;
  size_t used, queued, sent;
  struct client* next;
} client;
typedef struct request {
  int client;
  char* text;
  struct request* next;
} request;
static int listener = -1, serial, last_client;
static client* clients;
static request *head, *tail;
static size_t requests, request_bytes;
static char socket_path[sizeof(((struct sockaddr_un*)0)->sun_path)];
static dev_t socket_device;
static ino_t socket_inode;
static char* last;

int ot_dev_uid(void) { return (int)geteuid(); }
int ot_dev_private_directory(const char* path) {
  if (mkdir(path, 0700) != 0 && errno != EEXIST) return 0;
  struct stat st;
  return lstat(path, &st) == 0 && S_ISDIR(st.st_mode) && st.st_uid == geteuid() &&
         !(st.st_mode & 077);
}
int ot_dev_pid(void) { return (int)getpid(); }
const char* ot_dev_cwd(void) {
  static char path[4096];
  return getcwd(path, sizeof path) ? path : "";
}
const char* ot_dev_realpath(const char* path) {
  static char resolved[PATH_MAX];
  return realpath(path, resolved) ? resolved : path;
}
int64_t ot_dev_now(void) {
  struct timespec now;
  clock_gettime(CLOCK_REALTIME, &now);
  return (int64_t)now.tv_sec * 1000 + now.tv_nsec / 1000000;
}
int64_t ot_dev_mtime(const char* path) {
  struct stat st;
  if (stat(path, &st) != 0) return -1;
#ifdef __APPLE__
  return (int64_t)st.st_mtime * 1000 + st.st_mtimensec / 1000000;
#else
  return (int64_t)st.st_mtim.tv_sec * 1000 + st.st_mtim.tv_nsec / 1000000;
#endif
}
uint64_t ot_dev_clock(void) {
  struct timespec now;
  clock_gettime(CLOCK_MONOTONIC, &now);
  return (uint64_t)now.tv_sec * 1000 + now.tv_nsec / 1000000;
}
static int nonblock(int fd) {
  return fcntl(fd, F_SETFL, O_NONBLOCK) == 0 && fcntl(fd, F_SETFD, FD_CLOEXEC) == 0;
}
void ot_dev_close(void) {
  if (listener >= 0) close(listener);
  listener = -1;
  struct stat st;
  if (*socket_path && lstat(socket_path, &st) == 0 && st.st_dev == socket_device &&
      st.st_ino == socket_inode)
    unlink(socket_path);
  *socket_path = 0;
  while (clients) {
    client* next = clients->next;
    close(clients->fd);
    free(clients->input);
    free(clients->output);
    free(clients);
    clients = next;
  }
  while (head) {
    request* next = head->next;
    free(head->text);
    free(head);
    head = next;
  }
  tail = NULL;
  requests = request_bytes = 0;
  free(last);
  last = NULL;
}
int ot_dev_open(const char* path) {
  if (listener >= 0 || !*path || strlen(path) >= sizeof socket_path) return -1;
  struct sockaddr_un address = {0};
  address.sun_family = AF_UNIX;
  strcpy(address.sun_path, path);
  struct stat st;
  if (lstat(path, &st) == 0) {
    if (!S_ISSOCK(st.st_mode) || st.st_uid != geteuid()) return -1;
    int probe = socket(AF_UNIX, SOCK_STREAM, 0);
    if (probe < 0) return -1;
    int connected = connect(probe, (struct sockaddr*)&address, sizeof address);
    int error = errno;
    close(probe);
    if (connected == 0 || error != ECONNREFUSED) return -1;
    struct stat checked;
    if (lstat(path, &checked) != 0 || checked.st_dev != st.st_dev || checked.st_ino != st.st_ino)
      return -1;
    if (unlink(path) != 0) return -1;
  }
  int fd = socket(AF_UNIX, SOCK_STREAM, 0);
  if (fd < 0) return -1;
  mode_t mask = umask(0077);
  int bound = bind(fd, (struct sockaddr*)&address, sizeof address);
  umask(mask);
  if (bound != 0) {
    close(fd);
    return -1;
  }
  if (!nonblock(fd) || listen(fd, 32) != 0 || lstat(path, &st) != 0) {
    close(fd);
    unlink(path);
    return -1;
  }
  strcpy(socket_path, path);
  socket_device = st.st_dev;
  socket_inode = st.st_ino;
  listener = fd;
  return 0;
}
static void flush(client* c) {
  while (c->sent < c->queued) {
#ifdef MSG_NOSIGNAL
    ssize_t n = send(c->fd, c->output + c->sent, c->queued - c->sent, MSG_NOSIGNAL);
#else
    ssize_t n = send(c->fd, c->output + c->sent, c->queued - c->sent, 0);
#endif
    if (n < 0 && errno == EINTR) continue;
    if (n < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) return;
    if (n <= 0) {
      close(c->fd);
      c->fd = -1;
      return;
    }
    c->sent += (size_t)n;
  }
  free(c->output);
  c->output = NULL;
  c->sent = c->queued = 0;
}
void ot_dev_send(int id, const char* text) {
  size_t n = strlen(text);
  for (client* c = clients; c; c = c->next) {
    if (c->fd < 0 || (id && c->id != id)) continue;
    if (n > LIMIT || c->queued + n + 4 > LIMIT) {
      close(c->fd);
      c->fd = -1;
      continue;
    }
    unsigned char* p = realloc(c->output, c->queued + n + 4);
    if (!p) {
      close(c->fd);
      c->fd = -1;
      continue;
    }
    c->output = p;
    p += c->queued;
    p[0] = n >> 24;
    p[1] = n >> 16;
    p[2] = n >> 8;
    p[3] = n;
    memcpy(p + 4, text, n);
    c->queued += n + 4;
    flush(c);
  }
}
int ot_dev_poll(int timeout) {
  if (listener < 0) return 0;
  size_t count = 1;
  for (client* c = clients; c; c = c->next) count++;
  struct pollfd* fds = calloc(count, sizeof *fds);
  if (!fds) return head != NULL;
  fds[0] = (struct pollfd){listener, POLLIN, 0};
  size_t i = 1;
  for (client* c = clients; c; c = c->next)
    fds[i++] = (struct pollfd){c->fd, POLLIN | (c->queued ? POLLOUT : 0), 0};
  poll(fds, count, head ? 0 : timeout);
  i = 1;
  for (client* c = clients; c; c = c->next, i++) {
    if (c->fd < 0) continue;
    if (fds[i].revents & POLLOUT) flush(c);
    if (c->fd < 0 || !(fds[i].revents & (POLLIN | POLLHUP | POLLERR))) continue;
    unsigned char buffer[8192];
    ssize_t n = recv(c->fd, buffer, sizeof buffer, 0);
    if (n < 0 && (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR)) continue;
    if (n <= 0 || c->used + n > LIMIT + 4) {
      close(c->fd);
      c->fd = -1;
      continue;
    }
    unsigned char* p = realloc(c->input, c->used + n);
    if (!p) {
      close(c->fd);
      c->fd = -1;
      continue;
    }
    c->input = p;
    memcpy(p + c->used, buffer, n);
    c->used += n;
    while (c->used >= 4) {
      size_t len = (size_t)p[0] << 24 | (size_t)p[1] << 16 | (size_t)p[2] << 8 | p[3];
      if (len > LIMIT || requests >= 1024) {
        close(c->fd);
        c->fd = -1;
        break;
      }
      if (c->used < len + 4) break;
      if (memchr(p + 4, 0, len) || !ot_utf8_valid(p + 4, len) || request_bytes + len > 4 * LIMIT) {
        close(c->fd);
        c->fd = -1;
        break;
      }
      request* r = calloc(1, sizeof *r);
      if (!r || !(r->text = malloc(len + 1))) {
        free(r);
        close(c->fd);
        c->fd = -1;
        break;
      }
      memcpy(r->text, p + 4, len);
      r->text[len] = 0;
      r->client = c->id;
      if (tail) tail->next = r;
      else head = r;
      tail = r;
      requests++;
      request_bytes += len;
      c->used -= len + 4;
      memmove(p, p + len + 4, c->used);
    }
  }
  if (fds[0].revents & POLLIN) {
    int fd = accept(listener, NULL, NULL);
    if (fd >= 0) {
#ifdef SO_NOSIGPIPE
      int yes = 1;
      setsockopt(fd, SOL_SOCKET, SO_NOSIGPIPE, &yes, sizeof yes);
#endif
      client* c = calloc(1, sizeof *c);
      if (!c || !nonblock(fd)) {
        free(c);
        close(fd);
      } else {
        c->fd = fd;
        c->id = ++serial;
        c->next = clients;
        clients = c;
      }
    }
  }
  free(fds);
  client** link = &clients;
  while (*link) {
    client* c = *link;
    if (c->fd >= 0) link = &c->next;
    else {
      *link = c->next;
      free(c->input);
      free(c->output);
      free(c);
    }
  }
  return head != NULL;
}
const char* ot_dev_next(void) {
  free(last);
  last = NULL;
  if (!head) return NULL;
  request* r = head;
  head = r->next;
  if (!head) tail = NULL;
  request_bytes -= strlen(r->text);
  last = r->text;
  last_client = r->client;
  free(r);
  requests--;
  return last;
}
int ot_dev_client(void) { return last_client; }
