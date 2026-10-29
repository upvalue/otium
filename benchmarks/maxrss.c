/* Run a command and write its peak RSS to a file.

   usage: maxrss OUTPUT COMMAND [ARGUMENT ...]

   Linux carries a process's RSS high-water mark across exec, so a benchmark
   started straight from the Python runner reports at least the runner's own
   peak. This small process forks the command instead, so the floor is this
   process's size. It writes ru_maxrss as reported by wait4 (kilobytes on
   Linux, bytes on macOS) and exits with the command's status. */

#include <signal.h>
#include <stdio.h>
#include <sys/resource.h>
#include <sys/wait.h>
#include <unistd.h>

int main(int argc, char **argv) {
    if (argc < 3) {
        fprintf(stderr, "usage: maxrss OUTPUT COMMAND [ARGUMENT ...]\n");
        return 2;
    }
    pid_t child = fork();
    if (child < 0) {
        perror("maxrss: fork");
        return 2;
    }
    if (child == 0) {
        execvp(argv[2], argv + 2);
        perror("maxrss: exec");
        _exit(127);
    }
    int status;
    struct rusage usage;
    if (wait4(child, &status, 0, &usage) < 0) {
        perror("maxrss: wait4");
        return 2;
    }
    FILE *output = fopen(argv[1], "w");
    if (output) {
        fprintf(output, "%ld\n", usage.ru_maxrss);
        fclose(output);
    }
    if (WIFSIGNALED(status)) {
        return 128 + WTERMSIG(status);
    }
    return WEXITSTATUS(status);
}
