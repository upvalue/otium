#include "reader.h"
#include <ctype.h>
#include <errno.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>

struct ArenaBlock {
    ArenaBlock *next;
    size_t used, size;
    max_align_t data[];
};

static void fail(Reader *r, size_t offset, const char *message) {
    if (!r->error) { r->error = message; r->error_offset = offset; }
}

static void *allocate(Reader *r, size_t size) {
    const size_t alignment = _Alignof(max_align_t);
    if (size > SIZE_MAX - alignment) goto exhausted;
    size = (size + alignment - 1) / alignment * alignment;
    ArenaBlock *b = r->blocks;
    if (!b || size > b->size - b->used) {
        size_t capacity = size > 4096 ? size : 4096;
        if (capacity > SIZE_MAX - sizeof(*b)) goto exhausted;
        size_t bytes = sizeof(*b) + capacity;
        if (bytes > r->budget - r->allocated) goto exhausted;
        b = malloc(bytes);
        if (!b) goto exhausted;
        *b = (ArenaBlock){r->blocks, 0, capacity};
        r->blocks = b;
        r->allocated += bytes;
    }
    void *result = (unsigned char *)b->data + b->used;
    b->used += size;
    memset(result, 0, size);
    return result;
exhausted:
    fail(r, r->position, "reader memory limit exceeded");
    return NULL;
}

void reader_init(Reader *r, const char *source, size_t length, size_t budget) {
    *r = (Reader){.source = source, .length = length, .budget = budget};
}

void reader_destroy(Reader *r) {
    ArenaBlock *b = r->blocks;
    while (b) { ArenaBlock *next = b->next; free(b); b = next; }
    *r = (Reader){0};
}

static Ast *node(Reader *r, AstKind kind, size_t offset) {
    Ast *a = allocate(r, sizeof(*a));
    if (a) { a->kind = kind; a->offset = offset; }
    return a;
}

static Ast *text_node(Reader *r, AstKind kind, size_t offset,
                      const char *s, size_t n) {
    Ast *a = node(r, kind, offset);
    if (a) { a->as.text.data = s; a->as.text.size = n; }
    return a;
}

bool ast_symbol(const Ast *a, const char *s) {
    return a && a->kind == AST_SYMBOL && a->as.text.size == strlen(s)
        && !memcmp(a->as.text.data, s, a->as.text.size);
}

static bool space(unsigned char c) {
    return c == ' ' || c == '\t' || c == '\r' || c == '\n' || c == '\f' || c == '\v';
}

static bool delimiter(unsigned char c) {
    return space(c) || strchr("()[]{}\";'`,", c) != NULL;
}

static void skip(Reader *r) {
    while (r->position < r->length) {
        unsigned char c = r->source[r->position];
        if (space(c)) { r->position++; continue; }
        if (c != ';') break;
        while (r->position < r->length && r->source[r->position] != '\n'
               && r->source[r->position] != '\r') r->position++;
    }
}

static bool scalar(uint32_t c) {
    return c <= 0x10ffff && !(c >= 0xd800 && c <= 0xdfff);
}

static int hex(unsigned char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

static bool unicode_hex(const char *s, size_t n, uint32_t *value) {
    if (!n) return false;
    uint32_t v = 0;
    for (size_t i = 0; i < n; i++) {
        int digit = hex((unsigned char)s[i]);
        if (digit < 0 || v > (0x10ffffu - (unsigned)digit) / 16) return false;
        v = v * 16 + (unsigned)digit;
    }
    *value = v;
    return scalar(v);
}

static size_t utf8_decode(const char *s, size_t n, uint32_t *value) {
    if (!n) return 0;
    unsigned char c = (unsigned char)s[0];
    size_t count;
    uint32_t v, minimum;
    if (c < 0x80) { *value = c; return 1; }
    if (c >= 0xc2 && c <= 0xdf) { count = 2; v = c & 31; minimum = 0x80; }
    else if (c >= 0xe0 && c <= 0xef) { count = 3; v = c & 15; minimum = 0x800; }
    else if (c >= 0xf0 && c <= 0xf4) { count = 4; v = c & 7; minimum = 0x10000; }
    else return 0;
    if (count > n) return 0;
    for (size_t i = 1; i < count; i++) {
        c = (unsigned char)s[i];
        if ((c & 0xc0) != 0x80) return 0;
        v = (v << 6) | (c & 63);
    }
    if (v < minimum || !scalar(v)) return 0;
    *value = v;
    return count;
}

static size_t utf8_encode(char *out, uint32_t c) {
    if (c < 0x80) { out[0] = (char)c; return 1; }
    if (c < 0x800) {
        out[0] = (char)(0xc0 | c >> 6); out[1] = (char)(0x80 | (c & 63)); return 2;
    }
    if (c < 0x10000) {
        out[0] = (char)(0xe0 | c >> 12); out[1] = (char)(0x80 | ((c >> 6) & 63));
        out[2] = (char)(0x80 | (c & 63)); return 3;
    }
    out[0] = (char)(0xf0 | c >> 18); out[1] = (char)(0x80 | ((c >> 12) & 63));
    out[2] = (char)(0x80 | ((c >> 6) & 63)); out[3] = (char)(0x80 | (c & 63));
    return 4;
}

static Ast *string(Reader *r, size_t start) {
    /* Find the closing quote first so many short strings do not reserve the
     * remaining source length each time. Decoding never expands that span. */
    size_t end = r->position;
    for (; end < r->length; end++) {
        if (r->source[end] == '"') break;
        if (r->source[end] == '\\' && end + 1 < r->length) end++;
    }
    if (end == r->length) { fail(r, start, "unterminated string"); return NULL; }
    char *out = allocate(r, end - r->position + 1);
    if (!out) return NULL;
    size_t used = 0;
    while (r->position < end) {
        size_t at = r->position++;
        unsigned char c = r->source[at];
        if (c == '\\') {
            c = r->source[r->position++];
            switch (c) {
            case 'n': out[used++] = '\n'; break;
            case 'r': out[used++] = '\r'; break;
            case 't': out[used++] = '\t'; break;
            case '0': out[used++] = 0; break;
            case 'e': out[used++] = 27; break;
            case '"': case '\\': out[used++] = (char)c; break;
            case 'x': {
                size_t digits = r->position;
                while (r->position < end && r->source[r->position] != ';') r->position++;
                uint32_t value;
                if (r->position == end || !unicode_hex(r->source + digits,
                        r->position - digits, &value)) {
                    fail(r, at, "invalid Unicode string escape"); return NULL;
                }
                r->position++;
                used += utf8_encode(out + used, value);
                break;
            }
            default: fail(r, at, "invalid string escape"); return NULL;
            }
        } else if (c == '\r') {
            if (r->position < end && r->source[r->position] == '\n') r->position++;
            out[used++] = '\n';
        } else {
            uint32_t value;
            size_t count = utf8_decode(r->source + at, end - at, &value);
            if (!count || !value) { fail(r, at, "invalid source character"); return NULL; }
            memcpy(out + used, r->source + at, count);
            used += count;
            r->position = at + count;
        }
    }
    r->position = end + 1;
    return text_node(r, AST_STRING, start, out, used);
}

static Ast *character(Reader *r, size_t start) {
    size_t begin = r->position;
    if (begin == r->length) { fail(r, start, "missing character"); return NULL; }
    uint32_t value;
    size_t count = utf8_decode(r->source + begin, r->length - begin, &value);
    if (!count || !value) { fail(r, start, "invalid character"); return NULL; }
    r->position += count;
    if (!delimiter((unsigned char)r->source[begin]))
        while (r->position < r->length && !delimiter((unsigned char)r->source[r->position]))
            r->position++;
    size_t size = r->position - begin;
    if (size != count) {
        static const struct { const char *name; uint32_t value; } names[] = {
            {"space", 32}, {"newline", 10}, {"tab", 9}, {"return", 13}, {"nul", 0}
        };
        bool found = false;
        for (size_t i = 0; i < sizeof(names) / sizeof(names[0]); i++)
            if (strlen(names[i].name) == size && !memcmp(r->source + begin, names[i].name, size)) {
                value = names[i].value; found = true; break;
            }
        if (!found && r->source[begin] == 'x')
            found = unicode_hex(r->source + begin + 1, size - 1, &value);
        if (!found) { fail(r, start, "unknown character name"); return NULL; }
    }
    Ast *a = node(r, AST_CHAR, start);
    if (a) a->as.integer = value;
    return a;
}

static Ast *atom(Reader *r, size_t start) {
    while (r->position < r->length && !delimiter((unsigned char)r->source[r->position]))
        r->position++;
    size_t n = r->position - start;
    const char *s = r->source + start;
    if (!n) { fail(r, start, "invalid source character"); return NULL; }
    for (size_t i = 0; i < n;) {
        uint32_t value;
        size_t count = utf8_decode(s + i, n - i, &value);
        if (!count || !value) { fail(r, start + i, "invalid source character"); return NULL; }
        i += count;
    }
    if (n == 3 && !memcmp(s, "nil", 3)) return node(r, AST_NIL, start);
    if ((n == 2 && !memcmp(s, "#t", 2)) || (n == 5 && !memcmp(s, "#true", 5))) {
        Ast *a = node(r, AST_BOOL, start); if (a) a->as.integer = 1; return a;
    }
    if ((n == 2 && !memcmp(s, "#f", 2)) || (n == 6 && !memcmp(s, "#false", 6)))
        return node(r, AST_BOOL, start);
    if (*s == '#') { fail(r, start, "reserved # syntax"); return NULL; }
    if (*s == ':' && n == 1) { fail(r, start, "empty keyword"); return NULL; }
    size_t sign = (*s == '+' || *s == '-');
    bool numeric = sign < n && (isdigit((unsigned char)s[sign]) ||
        (s[sign] == '.' && sign + 1 < n && isdigit((unsigned char)s[sign + 1])));
    if (numeric) {
        size_t pos = sign;
        bool real = false;
        int base = 10;
        if (pos + 1 < n && s[pos] == '0' && (s[pos + 1] == 'x' || s[pos + 1] == 'X')) {
            base = 16; pos += 2;
            size_t digits = pos;
            while (pos < n && hex((unsigned char)s[pos]) >= 0) pos++;
            if (pos == digits) goto bad_number;
        } else {
            while (pos < n && isdigit((unsigned char)s[pos])) pos++;
            if (pos < n && s[pos] == '.') {
                real = true; pos++;
                while (pos < n && isdigit((unsigned char)s[pos])) pos++;
            }
            if (pos < n && (s[pos] == 'e' || s[pos] == 'E')) {
                real = true; pos++;
                if (pos < n && (s[pos] == '+' || s[pos] == '-')) pos++;
                size_t digits = pos;
                while (pos < n && isdigit((unsigned char)s[pos])) pos++;
                if (pos == digits) goto bad_number;
            }
        }
        if (pos != n) goto bad_number;
        char *token = allocate(r, n + 1);
        if (!token) return NULL;
        memcpy(token, s, n);
        Ast *a = node(r, real ? AST_FLOAT : AST_INT, start);
        if (!a) return NULL;
        errno = 0;
        if (real) {
            a->as.real = strtod(token, NULL);
            if (!isfinite(a->as.real)) goto bad_number;
        } else {
            a->as.integer = strtoll(token, NULL, base);
            if (errno == ERANGE) goto bad_number;
        }
        return a;
    }
    if (*s == '.') { fail(r, start, "invalid dot syntax"); return NULL; }
    return text_node(r, *s == ':' ? AST_KEYWORD : AST_SYMBOL, start, s, n);
bad_number:
    fail(r, start, "invalid or out-of-range number"); return NULL;
}

static Ast *parse(Reader *r);

static Ast *sequence(Reader *r, size_t start, char close, const char *constructor) {
    Ast *a = node(r, AST_LIST, start);
    if (!a) return NULL;
    Ast **next = &a->as.list.first;
    if (constructor) {
        *next = text_node(r, AST_SYMBOL, start, constructor, strlen(constructor));
        if (!*next) return NULL;
        next = &(*next)->next;
    }
    for (;;) {
        skip(r);
        if (r->position == r->length) { fail(r, start, "unterminated sequence"); return NULL; }
        char c = r->source[r->position];
        if (c == close) { r->position++; return a; }
        if (c == '.' && (r->position + 1 == r->length ||
            delimiter((unsigned char)r->source[r->position + 1]))) {
            if (!a->as.list.first) { fail(r, r->position, "dot needs a preceding element"); return NULL; }
            r->position++;
            a->as.list.tail = parse(r);
            if (!a->as.list.tail) return NULL;
            skip(r);
            if (r->position == r->length || r->source[r->position] != close) {
                fail(r, r->position, "expected closing delimiter after dotted tail"); return NULL;
            }
            r->position++;
            return a;
        }
        *next = parse(r);
        if (!*next) return NULL;
        next = &(*next)->next;
    }
}

static Ast *prefix(Reader *r, size_t start, const char *name) {
    Ast *a = node(r, AST_LIST, start);
    Ast *symbol = text_node(r, AST_SYMBOL, start, name, strlen(name));
    if (!a || !symbol) return NULL;
    a->as.list.first = symbol;
    symbol->next = parse(r);
    return symbol->next ? a : NULL;
}

static Ast *parse_inner(Reader *r) {
    skip(r);
    size_t start = r->position;
    if (start == r->length) { fail(r, start, "expected datum"); return NULL; }
    char c = r->source[r->position++];
    switch (c) {
    case '(': return sequence(r, start, ')', NULL);
    case '[': return sequence(r, start, ']', "array");
    case '{': return sequence(r, start, '}', "table");
    case ')': case ']': case '}': fail(r, start, "unexpected closing delimiter"); return NULL;
    case '"': return string(r, start);
    case '\'': return prefix(r, start, "quote");
    case '`': return prefix(r, start, "quasiquote");
    case ',':
        if (r->position < r->length && r->source[r->position] == '@') {
            r->position++; return prefix(r, start, "unquote-splicing");
        }
        return prefix(r, start, "unquote");
    case '#':
        if (r->position < r->length && r->source[r->position] == '\\') {
            r->position++; return character(r, start);
        }
        if (r->position < r->length && r->source[r->position] == '`') {
            r->position++; return prefix(r, start, "syntax-quote");
        }
        if (r->length - r->position >= 3 && !memcmp(r->source + r->position, "u8(", 3)) {
            r->position += 3; return sequence(r, start, ')', "bytevector");
        }
        break;
    }
    r->position = start;
    return atom(r, start);
}

static Ast *parse(Reader *r) {
    if (r->error) return NULL;
    if (r->depth == 512) { fail(r, r->position, "reader nesting limit exceeded"); return NULL; }
    r->depth++;
    Ast *a = parse_inner(r);
    r->depth--;
    return a;
}

Ast *reader_next(Reader *r) {
    if (r->error) return NULL;
    skip(r);
    return r->position == r->length ? NULL : parse(r);
}

Ast *reader_one(Reader *r) {
    Ast *a = reader_next(r);
    if (!a) { fail(r, r->position, "expected datum"); return NULL; }
    skip(r);
    if (r->position != r->length) { fail(r, r->position, "expected one datum"); return NULL; }
    return a;
}
