#include "runtime.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static Value eval(Runtime *rt, const char *source) {
    Value v;
    if (!runtime_eval(rt, source, strlen(source), &v)) {
        fprintf(stderr, "%s\n%s\n", source, runtime_error(rt)); abort();
    }
    return v;
}
static void integer(Runtime *rt, const char *source, intptr_t expected) {
    Value actual = eval(rt, source);
    if (actual != value_int(expected)) {
        fprintf(stderr, "%s: expected %ld, got ", source, (long)expected);
        runtime_print(rt, stderr, actual); fputc('\n', stderr); abort();
    }
}
static void string(Runtime *rt, const char *source, const char *expected) {
    Value v = eval(rt, source);
    FILE *file = tmpfile(); assert(file);
    runtime_print(rt, file, v);
    long n = ftell(file); assert(n >= 0);
    rewind(file);
    char *bytes = malloc((size_t)n + 1); assert(bytes);
    assert(fread(bytes, 1, (size_t)n, file) == (size_t)n);
    bytes[n] = 0;
    assert(!strcmp(bytes, expected));
    free(bytes); fclose(file);
}
static void rejects(Runtime *rt, const char *source, const char *message) {
    Value result;
    if (runtime_eval(rt, source, strlen(source), &result) || !strstr(runtime_error(rt), message)) {
        fprintf(stderr, "%s: expected %s, got %s\n", source, message, runtime_error(rt)); abort();
    }
    integer(rt, "(+ 20 22)", 42);
}

int main(int argc, char **argv) {
    bool native = false, sanitize = false;
    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "--native")) native = true;
        else if (!strcmp(argv[i], "--sanitize")) sanitize = true;
        else abort();
    }
    Runtime *rt = runtime_create(128 * 1024, 4096); assert(rt);
    runtime_native(rt, native ? 1 : 0);
    char source[2048];
    const char *suffix = sanitize ? "_sanitize" : "";
    snprintf(source, sizeof(source),
        "(def lib (ffi/open-library '(\"/missing/ffi/library\" \"./ffi_fixture%s.so\"))) "
        "(def other (ffi/open-library '(\"./ffi_other%s.so\"))) "
        "(def extra (ffi/open-library '(\"./ffi_extra%s.so\")))", suffix, suffix, suffix);
    eval(rt, source);
    integer(rt, "((ffi/bind lib \"fixture_id\" '() 'int #t))", 17);
    assert(runtime_foreign_signatures(rt) == 1);
    integer(rt, "((ffi/bind other \"fixture_id\" '() 'int #f))", 29);
    assert(runtime_foreign_signatures(rt) == 1);
    integer(rt, "(apply (ffi/bind lib \"fixture_id\" '() 'int #t) nil)", 17);

    assert(eval(rt, "(ffi/has-symbol? lib \"echo_bool\")") == V_TRUE);
    assert(eval(rt, "(ffi/has-symbol? lib \"absent\")") == V_FALSE);
    rejects(rt, "(ffi/open-library '(\"/missing/ffi/library\"))", "load");
    rejects(rt, "(ffi/bind lib \"absent\" '() 'int #t)", "symbol");
    rejects(rt, "(ffi/bind lib \"fixture_id\" '() 'int 0)", "boolean");
    rejects(rt, "(ffi/bind lib \"fixture_id\" '() 'int)", "arity");
    rejects(rt, "(ffi/bind lib \"fixture_id\" '(void) 'int #t)", "void argument");
    rejects(rt, "(ffi/bind lib \"fixture_id\" '(unknown) 'int #t)", "unsupported scalar");
    rejects(rt, "(ffi/bind lib \"fixture_id\" '(int . int) 'int #t)", "improper");
    rejects(rt, "(let ((p (list 'int))) (set-cdr! p p) (ffi/bind lib \"fixture_id\" p 'int #t))", "cyclic");

    static const struct { const char *type, *symbol; intptr_t value; } integers[] = {
        {"i8","i8",-128}, {"u8","u8",255}, {"i16","i16",-32768}, {"u16","u16",65535},
        {"i32","i32",-2147483647}, {"u32","u32",4294967295},
        {"i64","i64",-4611686018427387904}, {"u64","u64",4611686018427387903},
        {"short","short",-1234}, {"ushort","ushort",54321}, {"int","int",-1234567},
        {"uint","uint",1234567}, {"long","long",-1234567}, {"ulong","ulong",1234567},
        {"size-t","size_t",1234567}, {"ptrdiff-t","ptrdiff_t",-1234567}
    };
    for (size_t i = 0; i < sizeof(integers)/sizeof(integers[0]); i++) {
        snprintf(source, sizeof(source), "((ffi/bind lib \"echo_%s\" '(%s) '%s #t) %ld)",
            integers[i].symbol, integers[i].type, integers[i].type, (long)integers[i].value);
        integer(rt, source, integers[i].value);
        snprintf(source, sizeof(source), "(let ((p (ffi/alloc '%s 1))) (ffi/set! p '%s 0 %ld) "
            "(let ((v (ffi/ref p '%s 0))) (ffi/free! p) v))", integers[i].type, integers[i].type,
            (long)integers[i].value, integers[i].type);
        integer(rt, source, integers[i].value);
    }
    eval(rt, "(def byte (ffi/bind lib \"echo_u8\" '(u8) 'u8 #t))"
        "(def boolean (ffi/bind lib \"echo_bool\" '(bool) 'bool #t))"
        "(def single (ffi/bind lib \"echo_float\" '(float) 'float #t))"
        "(def real (ffi/bind lib \"echo_double\" '(double) 'double #f))"
        "(def text (ffi/bind lib \"echo_utf8\" '(utf8) 'utf8 #t))"
        "(def pointer (ffi/bind lib \"echo_pointer\" '(pointer) 'pointer #t))");
    assert(eval(rt, "(boolean #t)") == V_TRUE);
    assert(eval(rt, "(boolean #f)") == V_FALSE);
    rejects(rt, "(boolean 1)", "boolean");
    rejects(rt, "(boolean)", "arity");
    rejects(rt, "(byte 256)", "range");
    rejects(rt, "(byte -1)", "range");
    rejects(rt, "(byte 1.0)", "exact integer");
    assert(eval(rt, "(= (single 16777217) 16777216.0)") == V_TRUE);
    assert(eval(rt, "(= (real 1.25) 1.25)") == V_TRUE);
    rejects(rt, "(single 1e100)", "range");
    rejects(rt, "((ffi/bind lib \"high_u64\" '() 'u64 #t))", "range");
    integer(rt, "((ffi/bind lib \"calls\" '() 'int #t))", 1);
    rejects(rt, "((ffi/bind extra \"lowest_integer\" '() 'i64 #t))", "range");
    string(rt, "(text \"héllo 🌍\")", "héllo 🌍");
    string(rt, "(text \"\")", "");
    assert(eval(rt, "(ffi/null? (text ffi/null))") == V_TRUE);
    rejects(rt, "(text #f)", "string");
    rejects(rt, "(text \"a\\0b\")", "embedded NUL");
    rejects(rt, "((ffi/bind lib \"bad_utf8\" '() 'utf8 #t))", "encoding");
    eval(rt, "(def first ((ffi/bind lib \"changing_text\" '() 'utf8 #t)))");
    assert(eval(rt, "((ffi/bind lib \"change_text\" '() 'void #t))") == V_FALSE);
    string(rt, "first", "first");
    string(rt, "((ffi/bind lib \"changing_text\" '() 'utf8 #t))", "second");
    string(rt, "((ffi/bind extra \"offset_text\" '(utf8) 'utf8 #t) \"hello\")", "ello");
    rejects(rt, "((ffi/bind extra \"offset_text\" '(utf8) 'pointer #t) \"hello\")", "temporary UTF-8");
    assert(eval(rt, "(ffi/null? ffi/null)") == V_TRUE);
    assert(eval(rt, "(ffi/null? 0)") == V_FALSE);
    assert(eval(rt, "(ffi/pointer? ffi/null)") == V_TRUE);
    assert(eval(rt, "(if ffi/null #t #f)") == V_TRUE);
    assert(eval(rt, "(ffi/null? (pointer ffi/null))") == V_TRUE);
    rejects(rt, "(pointer 0)", "pointer");
    assert(eval(rt, "((ffi/bind lib \"null_callback\" '(callback-pointer) 'bool #t) ffi/null)") == V_TRUE);

    eval(rt, "(def p (ffi/alloc 'u64 2)) (def view (ffi/view p 8 8))");
    integer(rt, "(ffi/allocation-count)", 1);
    integer(rt, "(ffi/allocation-bytes)", 16);
    integer(rt, "(ffi/ref p 'u64 0)", 0);
    eval(rt, "(ffi/set! view 'u64 0 1234)");
    integer(rt, "(ffi/ref p 'u64 1)", 1234);
    rejects(rt, "(ffi/ref p 'u64 2)", "bounds");
    rejects(rt, "(ffi/ref (ffi/view p 1 8) 'u64 0)", "alignment");
    rejects(rt, "(ffi/view view 0 9)", "bounds");
    rejects(rt, "(ffi/free! view)", "owning");
    eval(rt, "(def alias (pointer p))");
    assert(eval(rt, "(ffi/pointer=? p alias)") == V_TRUE);
    eval(rt, "(ffi/free! p)");
    assert(eval(rt, "(ffi/free! p)") == V_FALSE);
    rejects(rt, "(ffi/ref view 'u64 0)", "released");
    rejects(rt, "(pointer alias)", "released");
    integer(rt, "(ffi/allocation-count)", 0);
    integer(rt, "(ffi/allocation-bytes)", 0);
    eval(rt, "(def zero (ffi/alloc 'u8 0))");
    rejects(rt, "(ffi/ref zero 'u8 0)", "bounds");
    eval(rt, "(ffi/free! zero)");
    eval(rt, "(def storage (ffi/alloc-utf8 \"retained λ\"))"
        "((ffi/bind lib \"retain\" '(pointer) 'void #t) storage)");
    runtime_gc_stress(rt, true);
    string(rt, "((ffi/bind lib \"retained_text\" '() 'utf8 #t))", "retained λ");
    string(rt, "(ffi/read-utf8 storage 12)", "retained λ");
    rejects(rt, "(ffi/read-utf8 storage 2)", "bounds");
    string(rt, "(text (text \"moves across GC\"))", "moves across GC");
    assert(eval(rt, "(= (real (single 1.25)) 1.25)") == V_TRUE);
    eval(rt, "(def interior ((ffi/bind extra \"offset_pointer\" '(pointer) 'pointer #t) storage))");
    string(rt, "(ffi/read-utf8 interior 11)", "etained λ");
    eval(rt, "(ffi/free! storage)");
    rejects(rt, "(ffi/read-utf8 interior 11)", "released");
    eval(rt, "(def slots (ffi/alloc 'pointer 1))"
        "((ffi/bind lib \"pointer_out\" '(pointer pointer) 'void #t) slots slots)");
    assert(eval(rt, "(ffi/pointer=? slots (ffi/ref slots 'pointer 0))") == V_TRUE);
    eval(rt, "(ffi/free! slots)");
    eval(rt, "(def mix (ffi/bind extra \"abi_mix\" "
        "'(int double int double int double int double int double int double int double int double pointer bool float) 'double #t))");
    assert(eval(rt, "(= (mix 1 0.5 2 1.5 3 2.5 4 3.5 5 4.5 6 5.5 7 6.5 8 7.5 ffi/null #t 0.25) 168.25)") == V_TRUE);
    eval(rt, "(def (tail-call x) (byte x))");
    integer(rt, "(tail-call 42)", 42);
    Value argument = value_int(43), result;
    assert(runtime_call(rt, "byte", &argument, 1, &result) && result == argument);
    integer(rt, "(ffi/allocation-count)", 0);
    assert(runtime_collections(rt) > 10);
    if (native) assert(runtime_native_entries(rt) > 0);
    runtime_destroy(rt);
    rt = runtime_create(8192, 256); assert(rt);
    runtime_native(rt, native ? 1 : 0);
    snprintf(source, sizeof(source), "(def lib (ffi/open-library '(\"./ffi_fixture%s.so\"))) "
        "(def text (ffi/bind lib \"echo_utf8\" '(utf8) 'utf8 #t))", suffix);
    eval(rt, source);
    string(rt, "(let loop ((n 10000) (value (text \"retained\"))) "
        "(if (= n 0) value (loop (- n 1) (text value))))", "retained");
    assert(runtime_collections(rt) > 10);
    runtime_destroy(rt);
    rt = runtime_create(128, 64); assert(rt);
    runtime_native(rt, native ? 1 : 0);
    const char *exhaustion = "(ffi/alloc 'u8 5)";
    assert(!runtime_eval(rt, exhaustion, strlen(exhaustion), &result));
    assert(strstr(runtime_error(rt), "heap exhausted"));
    assert(runtime_call(rt, "ffi/allocation-count", NULL, 0, &result) && result == value_int(0));
    assert(runtime_call(rt, "ffi/allocation-bytes", NULL, 0, &result) && result == value_int(0));
    runtime_destroy(rt);
    puts("foreign call tests passed");
}
