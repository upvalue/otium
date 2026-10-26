#define _POSIX_C_SOURCE 200809L
#include "internal.h"
#include <dlfcn.h>
#include <errno.h>
#include <fcntl.h>
#include <spawn.h>
#include <stdlib.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

#ifndef OTIUM_NATIVE_INCLUDE
#define OTIUM_NATIVE_INCLUDE "."
#endif
extern char **environ;

static double monotonic(void) {
    struct timespec now;
    if (clock_gettime(CLOCK_MONOTONIC, &now)) return 0;
    return (double)now.tv_sec + (double)now.tv_nsec / 1e9;
}

bool rt_module_compile(Runtime *rt, bool (*emit)(Runtime *, FILE *, void *), void *context,
                       const char *entry_name, void **handle, void **entry, double *seconds) {
    double start = monotonic();
    bool ok = true;
    *handle = NULL; *entry = NULL;
    char directory[] = "/tmp/otium-native-XXXXXX";
    if (!mkdtemp(directory)) return rt_error(rt, "cannot create native compiler directory");
    char source[128], library[128], log[128];
    snprintf(source, sizeof(source), "%s/code.c", directory);
    snprintf(library, sizeof(library), "%s/code.so", directory);
    snprintf(log, sizeof(log), "%s/compiler.log", directory);
    FILE *out = fopen(source, "w");
    if (!out) ok = rt_error(rt, "cannot create native source");
    else {
        ok = emit(rt, out, context);
        if (fclose(out)) ok = false;
        if (!ok) rt_error(rt, "cannot write native source");
    }
    if (ok) {
        const char *compiler = getenv("OTIUM_NATIVE_CC");
        if (!compiler || !*compiler) compiler = "cc";
        const char *sanitize = getenv("OTIUM_NATIVE_SANITIZE");
        char *args[] = {(char *)compiler, "-O3", "-std=c11", "-ffp-contract=off", "-fPIC", "-shared",
            "-I", OTIUM_NATIVE_INCLUDE, source, "-o", library,
            sanitize && *sanitize ? "-fsanitize=address,undefined" : "-fno-math-errno", NULL};
        posix_spawn_file_actions_t actions;
        int status = posix_spawn_file_actions_init(&actions);
        bool initialized = status == 0;
        if (!status) status = posix_spawn_file_actions_addopen(&actions, STDERR_FILENO, log,
            O_WRONLY | O_CREAT | O_TRUNC, 0600);
        pid_t child;
        if (!status) status = posix_spawnp(&child, compiler, &actions, NULL, args, environ);
        if (initialized) posix_spawn_file_actions_destroy(&actions);
        if (status) ok = rt_error(rt, "cannot start native C compiler (OTIUM_NATIVE_CC)");
        else {
            int outcome;
            pid_t waited;
            do { waited = waitpid(child, &outcome, 0); } while (waited < 0 && errno == EINTR);
            if (waited < 0 || !WIFEXITED(outcome) || WEXITSTATUS(outcome)) {
                FILE *errors = fopen(log, "r");
                if (errors) {
                    char line[512];
                    while (fgets(line, sizeof(line), errors)) fputs(line, stderr);
                    fclose(errors);
                }
                ok = rt_error(rt, "native C compilation failed");
            }
        }
    }
    if (ok) {
        *handle = dlopen(library, RTLD_NOW | RTLD_LOCAL);
        if (!*handle) ok = rt_error(rt, dlerror());
        else {
            *entry = dlsym(*handle, entry_name);
            if (!*entry) {
                dlclose(*handle); *handle = NULL;
                ok = rt_error(rt, "missing compiled module entry point");
            }
        }
    }
    unlink(source); unlink(library); unlink(log); rmdir(directory);
    if (seconds) *seconds += monotonic() - start;
    return ok;
}
