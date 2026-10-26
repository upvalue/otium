#include "runtime.h"
#include <errno.h>
#include <stdlib.h>
#include <string.h>

static bool size_option(const char *text, size_t *out) {
    char *end;
    errno = 0;
    unsigned long long value = strtoull(text, &end, 10);
    if (errno || !*text || *text == '-' || *end || value > SIZE_MAX || !value) return false;
    *out = (size_t)value;
    return true;
}

int main(int argc, char **argv) {
    size_t heap = 1024 * 1024, stack = 128 * 1024;
    size_t native = 0;
    bool stats = false, echo = false;
    const char *source = NULL, *path = NULL;
    for (int i = 1; i < argc; i++) {
        if (!strncmp(argv[i], "--heap=", 7)) {
            if (!size_option(argv[i] + 7, &heap)) goto usage;
        } else if (!strncmp(argv[i], "--stack=", 8)) {
            if (!size_option(argv[i] + 8, &stack)) goto usage;
        } else if (!strcmp(argv[i], "-L") && i + 1 < argc) i++;
        else if (!strcmp(argv[i], "--stats")) stats = true;
        else if (!strcmp(argv[i], "--native")) native = 1024;
        else if (!strncmp(argv[i], "--native=", 9)) {
            if (!size_option(argv[i] + 9, &native)) goto usage;
        }
        else if (!strcmp(argv[i], "-e") && i + 1 < argc && !source && !path) {
            source = argv[++i]; echo = true;
        } else if (argv[i][0] != '-' && !source && !path) path = argv[i];
        else goto usage;
    }
    if (!source && !path) goto usage;
    char *buffer = NULL;
    size_t length;
    if (path) {
        FILE *file = fopen(path, "rb");
        if (!file) { perror(path); return 1; }
        if (fseek(file, 0, SEEK_END)) { perror(path); fclose(file); return 1; }
        long bytes = ftell(file);
        if (bytes < 0 || (unsigned long)bytes > SIZE_MAX - 1) { fclose(file); return 1; }
        rewind(file);
        buffer = malloc((size_t)bytes + 1);
        if (!buffer) { fclose(file); return 1; }
        length = fread(buffer, 1, (size_t)bytes, file);
        bool ok = length == (size_t)bytes && !ferror(file);
        fclose(file);
        if (!ok) { free(buffer); return 1; }
        buffer[length] = 0;
        source = buffer;
    } else length = strlen(source);
    Runtime *rt = runtime_create(heap, stack);
    if (!rt) { fputs("cannot initialize runtime\n", stderr); free(buffer); return 1; }
    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "-e")) { i++; continue; }
        if (!strcmp(argv[i], "-L")) {
            if (!runtime_load_path(rt, argv[++i])) {
                fprintf(stderr, "%s\n", runtime_error(rt)); runtime_destroy(rt); free(buffer); return 1;
            }
        }
    }
    Value result;
    runtime_native(rt, native);
    bool ok = runtime_eval(rt, source, length, &result);
    if (!ok) fprintf(stderr, "%s:%zu: %s\n", path ? path : "<expression>",
        runtime_error_offset(rt), runtime_error(rt));
    else if (echo) { runtime_print(rt, stdout, result); fputc('\n', stdout); }
    if (stats) fprintf(stderr, "collections=%zu max-frames=%zu\n",
        runtime_collections(rt), runtime_max_frames(rt));
    if (stats && native) fprintf(stderr, "native-functions=%zu native-entries=%zu compile-seconds=%.6f\n",
        runtime_native_functions(rt), runtime_native_entries(rt), runtime_native_compile_seconds(rt));
    if (stats && runtime_foreign_signatures(rt)) fprintf(stderr, "ffi-signatures=%zu ffi-compile-seconds=%.6f\n",
        runtime_foreign_signatures(rt), runtime_foreign_compile_seconds(rt));
    runtime_destroy(rt);
    free(buffer);
    return ok ? 0 : 1;
usage:
    fputs("usage: otium-proto [--heap=BYTES] [--stack=SLOTS] [--native[=HEAT]] [--stats] [-L DIR] FILE | -e SOURCE\n", stderr);
    return 2;
}
