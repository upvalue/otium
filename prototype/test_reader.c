#include "reader.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static char *read_file(const char *path, size_t *length) {
    FILE *f = fopen(path, "rb");
    if (!f) { perror(path); exit(1); }
    assert(!fseek(f, 0, SEEK_END));
    long size = ftell(f);
    assert(size >= 0);
    rewind(f);
    char *s = malloc((size_t)size + 1);
    assert(s);
    assert(fread(s, 1, (size_t)size, f) == (size_t)size);
    assert(!fclose(f));
    s[size] = 0;
    *length = (size_t)size;
    return s;
}

/* Evaluate only the fixture's expected-value notation, not Otium programs. */
static bool same(const Ast *actual, const Ast *expected, bool evaluate) {
    if (!actual || !expected) return actual == expected;
    if (evaluate && expected->kind == AST_LIST) {
        const Ast *head = expected->as.list.first;
        if (ast_symbol(head, "quote")) return same(actual, head->next, false);
        if (ast_symbol(head, "integer->char"))
            return actual->kind == AST_CHAR && head->next->kind == AST_INT
                && actual->as.integer == head->next->as.integer;
        assert(ast_symbol(head, "list"));
        if (actual->kind != AST_LIST || actual->as.list.tail) return false;
        const Ast *a = actual->as.list.first, *b = head->next;
        for (; a && b; a = a->next, b = b->next)
            if (!same(a, b, true)) return false;
        return !a && !b;
    }
    if (actual->kind != expected->kind) return false;
    switch (actual->kind) {
    case AST_NIL: return true;
    case AST_BOOL: case AST_CHAR: case AST_INT: case AST_VALUE:
        return actual->as.integer == expected->as.integer;
    case AST_FLOAT: return actual->as.real == expected->as.real;
    case AST_STRING: case AST_SYMBOL: case AST_KEYWORD:
        return actual->as.text.size == expected->as.text.size &&
            !memcmp(actual->as.text.data, expected->as.text.data, actual->as.text.size);
    case AST_LIST: {
        const Ast *a = actual->as.list.first, *b = expected->as.list.first;
        for (; a && b; a = a->next, b = b->next)
            if (!same(a, b, false)) return false;
        return !a && !b && same(actual->as.list.tail, expected->as.list.tail, false);
    }
    }
    return false;
}

static void fixtures(void) {
    size_t length;
    char *source = read_file("../tests/reader/data/reader-datums.ot", &length);
    Reader fixtures;
    reader_init(&fixtures, source, length, 1024 * 1024);
    Ast *form;
    unsigned positive = 0, negative = 0;
    while ((form = reader_next(&fixtures))) {
        if (form->kind != AST_LIST) continue;
        Ast *head = form->as.list.first;
        if (!ast_symbol(head, "def")) continue;
        Ast *name = head->next, *values = name->next;
        bool accept = ast_symbol(name, "positive");
        assert(accept || ast_symbol(name, "negative"));
        assert(values->kind == AST_LIST && ast_symbol(values->as.list.first, "list"));
        for (Ast *entry = values->as.list.first->next; entry; entry = entry->next) {
            Ast *text = accept ? entry->as.list.first->next : entry;
            assert(text->kind == AST_STRING);
            Reader r;
            reader_init(&r, text->as.text.data, text->as.text.size, 65536);
            Ast *actual = reader_one(&r);
            bool ok = accept ? actual && same(actual, text->next, true) : !actual && r.error;
            if (!ok) {
                fprintf(stderr, "fixture failed: %.*s (%s)\n", (int)text->as.text.size,
                        text->as.text.data, r.error ? r.error : "value mismatch");
                abort();
            }
            if (accept) positive++; else negative++;
            reader_destroy(&r);
        }
    }
    assert(!fixtures.error && positive > 50 && negative > 50);
    printf("public reader fixtures: %u values, %u rejections passed\n", positive, negative);
    reader_destroy(&fixtures);
    free(source);
}

static void reject(const char *source, size_t length, size_t budget) {
    Reader r;
    reader_init(&r, source, length, budget);
    assert(!reader_one(&r) && r.error);
    assert(!reader_next(&r));
    reader_destroy(&r);
}

static void limits(void) {
    reject("1", 1, 1);
    reject("\0", 1, 65536);
    reject("\xc0\xaf", 2, 65536);
    reject("\"\xed\xa0\x80\"", 5, 65536);
    reject("9223372036854775808", 19, 65536);
    reject("-9223372036854775809", 20, 65536);
    reject("1e999", 5, 65536);
    char deep[600];
    memset(deep, '\'', sizeof(deep));
    reject(deep, sizeof(deep), 1024 * 1024);
    Reader r;
    const char *source = "-9223372036854775808 9223372036854775807\n(x]";
    reader_init(&r, source, strlen(source), 65536);
    assert(reader_next(&r)->as.integer == INT64_MIN);
    assert(reader_next(&r)->as.integer == INT64_MAX);
    assert(!reader_next(&r));
    assert(r.error_offset == strlen(source) - 1);
    reader_destroy(&r);
}

static void malformed_inputs(void) {
    static const char alphabet[] = "()[]{}\";'`,@#\\:0129xabcdef+-./ \r\n\t";
    uint32_t state = 0x183da5u;
    for (unsigned trial = 0; trial < 50000; trial++) {
        char source[128];
        state = state * 1664525u + 1013904223u;
        size_t length = state % sizeof(source);
        for (size_t i = 0; i < length; i++) {
            state = state * 1664525u + 1013904223u;
            source[i] = trial & 1 ? alphabet[(state >> 16) % (sizeof(alphabet) - 1)]
                : (char)(state >> 24);
        }
        Reader r;
        reader_init(&r, source, length, trial % 7 ? 65536 : 4200);
        size_t before = 0;
        while (reader_next(&r)) {
            assert(r.position > before && r.position <= length);
            before = r.position;
        }
        assert(r.position <= length && r.error_offset <= length);
        assert(r.allocated <= r.budget);
        reader_destroy(&r);
    }
}

int main(int argc, char **argv) {
    fixtures();
    limits();
    malformed_inputs();
    for (int i = 1; i < argc; i++) {
        size_t length, forms = 0;
        char *source = read_file(argv[i], &length);
        Reader r;
        reader_init(&r, source, length, 64 * 1024 * 1024);
        while (reader_next(&r)) forms++;
        if (r.error) {
            fprintf(stderr, "%s:%zu: %s\n", argv[i], r.error_offset, r.error);
            return 1;
        }
        printf("%s: %zu forms (%zu arena bytes)\n", argv[i], forms, r.allocated);
        reader_destroy(&r);
        free(source);
    }
    puts("reader tests passed");
}
