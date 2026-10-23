// Process and terminal operations manse cannot do through the FFI.
//
// Starting a child on a PTY needs setsid and TIOCSCTTY between fork and exec,
// where only async-signal-safe calls are allowed; macOS's posix_spawn has no
// file action for a controlling terminal. ioctl is variadic, which the FFI
// cannot call yet. Everything else about PTYs lives in manse/pty.ot.

#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <sys/ioctl.h>
#include <sys/wait.h>
#include <termios.h>
#include <unistd.h>

// Fork, give the child tty_path as its controlling terminal and standard
// streams, and exec path. Returns the child's pid, or -errno if fork fails.
// A failure in the child after fork exits it with status 126, or 127 if exec
// fails; the parent sees that through waitpid.
int manse_pty_spawn(const char* path, char* const argv[], char* const envp[],
                    const char* tty_path) {
  pid_t pid = fork();
  if (pid < 0) return -errno;
  if (pid > 0) return pid;

  // The child inherits the runtime's signal mask and handlers. Restore defaults
  // so the shell's job control and Ctrl-C behave normally.
  sigset_t none;
  sigemptyset(&none);
  sigprocmask(SIG_SETMASK, &none, NULL);
  struct sigaction action = {.sa_handler = SIG_DFL};
  sigemptyset(&action.sa_mask);
  for (int sig = 1; sig < NSIG; sig++) {
    if (sig != SIGKILL && sig != SIGSTOP) sigaction(sig, &action, NULL);
  }

  if (setsid() < 0) _exit(126);
  int fd = open(tty_path, O_RDWR);
  if (fd < 0 || ioctl(fd, TIOCSCTTY, 0) < 0) _exit(126);
  for (int target = 0; target < 3; target++) {
    if (dup2(fd, target) < 0) _exit(126);
  }
  // Keep only the standard streams, so no other PTY or socket leaks into the child.
  long limit = sysconf(_SC_OPEN_MAX);
  if (limit < 0 || limit > 65536) limit = 65536;
  for (int other = 3; other < limit; other++) close(other);

  execve(path, argv, envp);
  _exit(127);
}

// Set the PTY's size in cells and pixels. Returns 0 or -errno.
int manse_pty_resize(int fd, int cols, int rows, int width, int height) {
  struct winsize size = {
      .ws_row = (unsigned short)rows,
      .ws_col = (unsigned short)cols,
      .ws_xpixel = (unsigned short)width,
      .ws_ypixel = (unsigned short)height,
  };
  return ioctl(fd, TIOCSWINSZ, &size) < 0 ? -errno : 0;
}

// Preserve errno before returning through the FFI.
long manse_pty_write(int fd, const void* data, size_t size) {
  ssize_t n;
  do {
    n = write(fd, data, size);
  } while (n < 0 && errno == EINTR);
  return n < 0 ? -errno : n;
}

int manse_pty_waitpid(int pid, int* status, int flags) {
  int result;
  do {
    result = waitpid(pid, status, flags);
  } while (result < 0 && errno == EINTR);
  return result < 0 ? -errno : result;
}
