#include "internal.h"
#include <dlfcn.h>
#include <inttypes.h>
#include <stdlib.h>
#include <string.h>

typedef struct { size_t *height, maximum; } Layout;

static bool edge(Runtime *rt, Function *f, Layout *layout, size_t *queue,
                 size_t *end, size_t target, size_t height) {
    if (target >= f->count) return rt_error(rt, "native verifier: invalid jump");
    if (layout->height[target] == SIZE_MAX) {
        layout->height[target] = height;
        queue[(*end)++] = target;
    } else if (layout->height[target] != height)
        return rt_error(rt, "native verifier: inconsistent stack height");
    return true;
}

static bool analyze(Runtime *rt, Function *f, Layout *layout) {
    if (!f->count || f->count > SIZE_MAX / sizeof(size_t))
        return rt_error(rt, "native verifier: invalid function size");
    layout->height = malloc(f->count * sizeof(size_t));
    size_t *queue = malloc(f->count * sizeof(size_t));
    if (!layout->height || !queue) { free(queue); return rt_error(rt, "out of host memory"); }
    for (size_t i = 0; i < f->count; i++) layout->height[i] = SIZE_MAX;
    size_t start = 0, end = 1;
    queue[0] = 0; layout->height[0] = 0;
    bool ok = true;
    while (start < end && ok) {
        size_t pc = queue[start++], h = layout->height[pc], need = 0;
        Instruction in = f->code[pc];
        bool terminal = false;
        int change = 0;
        switch (in.op) {
        case OP_CONST: ok = in.arg < rt->constant_count; change = 1; break;
        case OP_GLOBAL: ok = in.arg < rt->global_count; change = 1; break;
        case OP_LOCAL: case OP_PRESENT: ok = in.arg < f->locals; change = 1; break;
        case OP_CLEAR_LOCAL: ok = in.arg < f->locals; break;
        case OP_UPVALUE: ok = in.arg < f->capture_count; change = 1; break;
        case OP_CLOSURE: ok = in.arg < rt->function_count; change = 1; break;
        case OP_SET_GLOBAL: case OP_DEFINE_GLOBAL:
            ok = in.arg < rt->global_count; need = 1; change = -1; break;
        case OP_BIND: case OP_SET_LOCAL:
            ok = in.arg < f->locals; need = 1; change = -1; break;
        case OP_SET_UPVALUE: ok = in.arg < f->capture_count; need = 1; change = -1; break;
        case OP_POP: need = 1; change = -1; break;
        case OP_DUP: need = 1; change = 1; break;
        case OP_CALL: case OP_TAIL_CALL:
            need = (size_t)in.arg + 1; terminal = in.op == OP_TAIL_CALL; break;
        case OP_RETURN: need = 1; terminal = true; ok = h == 1; break;
        case OP_JFALSE: need = 1; change = -1; break;
        case OP_JUMP: break;
        default: ok = false; break;
        }
        if (!ok || h < need) { ok = rt_error(rt, "native verifier: invalid instruction or stack underflow"); break; }
        size_t after = change < 0 ? h - 1 : h + (size_t)change;
        if (in.op == OP_CALL) after = h - in.arg;
        if (after > rt->stack_limit) { ok = rt_error(rt, "VM stack limit exceeded"); break; }
        if (h > layout->maximum) layout->maximum = h;
        if (after > layout->maximum) layout->maximum = after;
        if (terminal) continue;
        if (in.op == OP_JUMP || in.op == OP_JFALSE)
            ok = edge(rt, f, layout, queue, &end, in.arg, after);
        if (ok && in.op != OP_JUMP)
            ok = edge(rt, f, layout, queue, &end, pc + 1, after);
    }
    free(queue);
    return ok;
}

/* Recover a global name or compiler-supplied builtin through straight-line stack operations.
 * The emitted path still checks the function value saved before argument
 * evaluation. Arguments may redefine that global. */
static int call_hint(Runtime *rt, Function *fn, size_t pc, size_t argc) {
    size_t skip = argc;
    while (pc) {
        Instruction in = fn->code[--pc];
        switch (in.op) {
        case OP_CONST: case OP_GLOBAL: case OP_LOCAL: case OP_UPVALUE: case OP_CLOSURE:
            if (!skip) {
                if (in.op == OP_CONST && in.arg < rt->constant_count &&
                    (rt->constants[in.arg] & 255) == TAG_BUILTIN)
                    return (int)(rt->constants[in.arg] >> 8);
                return in.op == OP_GLOBAL && in.arg < rt->global_count ? rt_builtin_index(rt->names[in.arg]) : -1;
            }
            skip--; break;
        case OP_CLEAR_LOCAL: break;
        case OP_CALL:
            if (!skip || skip > SIZE_MAX - in.arg) return -1;
            skip += in.arg; break;
        default: return -1;
        }
    }
    return -1;
}

static bool scalar_builtin(int id) {
    return id == B_ADD || id == B_SUB || id == B_MUL || (id >= B_LT && id <= B_NUMEQ);
}

static bool scalar_eligible(Runtime *rt, Function *fn, const Layout *layout, bool numeric) {
    if (fn->rest || fn->optional || fn->count > 4096 ||
        fn->locals >= 128 || layout->maximum >= 128 ||
        fn->locals + layout->maximum + 1 > 128) return false;
    for (size_t i = 0; i < fn->locals; i++) if (fn->boxed[i]) return false;
    bool loop = false;
    for (size_t pc = 0; pc < fn->count; pc++) {
        if (layout->height[pc] == SIZE_MAX) continue;
        Instruction in = fn->code[pc];
        switch (in.op) {
        case OP_CONST:
            if (value_is_ptr(rt->constants[in.arg]) &&
                !(numeric && rt_kind(rt->constants[in.arg], K_FLOAT))) return false;
            break;
        case OP_GLOBAL: case OP_LOCAL: case OP_BIND: case OP_SET_LOCAL:
        case OP_CLEAR_LOCAL: case OP_UPVALUE: case OP_RETURN: case OP_POP:
        case OP_DUP: case OP_JFALSE: break;
        case OP_JUMP: if (in.arg <= pc) loop = true; break;
        case OP_CALL: case OP_TAIL_CALL: {
            int hint = call_hint(rt, fn, pc, in.arg);
            if (scalar_builtin(hint) && in.arg >= 2 && in.arg <= 8) break;
            if (in.op == OP_TAIL_CALL && hint < 0 && in.arg == fn->arity) { loop = true; break; }
            return false;
        }
        default: return false;
        }
    }
    return loop;
}

/* Only local mutation and allocation-free arithmetic are admitted. On a failed
 * check, the original VM frame can be replayed without duplicating effects. */
static void emit_scalar(Runtime *rt, FILE *out, Function *fn, const Layout *layout) {
    if (!scalar_eligible(rt, fn, layout, false)) return;
    size_t locals = fn->locals + 1;
    fputs("if(rt->frames[frame].ip==0", out);
    for (size_t i = 0; i < fn->arity; i++) fprintf(out, " && value_is_int(s[%zu])", i + 1);
    for (size_t pc = 0; pc < fn->count; pc++) {
        Instruction in = fn->code[pc];
        if (in.op != OP_GLOBAL || layout->height[pc] == SIZE_MAX) continue;
        int id = rt_builtin_index(rt->names[in.arg]);
        if (scalar_builtin(id)) fprintf(out, " && rt->globals[%u]==%u", in.arg, ((unsigned)id << 8) | TAG_BUILTIN);
    }
    fprintf(out, ") {\nrt->native_scalar_entries++; Value p[%zu]; memcpy(p,s,%zu*sizeof(Value));\n",
        locals + layout->maximum, locals);
    for (size_t pc = 0; pc < fn->count; pc++) {
        if (layout->height[pc] == SIZE_MAX) continue;
        size_t top = locals + layout->height[pc];
        Instruction in = fn->code[pc];
        fprintf(out, "P%zu:;\n", pc);
        switch (in.op) {
        case OP_CONST:
            fprintf(out, "p[%zu]=(Value)UINT64_C(%" PRIu64 ");\n", top, (uint64_t)rt->constants[in.arg]); break;
        case OP_GLOBAL: {
            int id = rt_builtin_index(rt->names[in.arg]);
            if (scalar_builtin(id)) fprintf(out, "p[%zu]=%u;\n", top, ((unsigned)id << 8) | TAG_BUILTIN);
            else fprintf(out, "p[%zu]=rt->globals[%u]; if(p[%zu]==V_UNBOUND) goto scalar_fallback;\n", top, in.arg, top);
            break;
        }
        case OP_LOCAL:
            fprintf(out, "p[%zu]=p[%u]; if(p[%zu]==V_UNBOUND) goto scalar_fallback;\n", top, in.arg + 1, top); break;
        case OP_BIND: case OP_SET_LOCAL:
            fprintf(out, "p[%u]=p[%zu];\n", in.arg + 1, top - 1); break;
        case OP_CLEAR_LOCAL: fprintf(out, "p[%u]=V_UNBOUND;\n", in.arg + 1); break;
        case OP_UPVALUE:
            fprintf(out, "p[%zu]=object_fields(object_fields(p[0])[%u])[0]; if(p[%zu]==V_UNBOUND) goto scalar_fallback;\n",
                top, in.arg, top); break;
        case OP_POP: break;
        case OP_DUP: fprintf(out, "p[%zu]=p[%zu];\n", top, top - 1); break;
        case OP_JUMP: fprintf(out, "goto P%u;\n", in.arg); break;
        case OP_JFALSE:
            fprintf(out, "if(p[%zu]==V_FALSE || p[%zu]==V_NIL) goto P%u;\n", top - 1, top - 1, in.arg); break;
        case OP_RETURN: fprintf(out, "return rt_leave(rt,p[%zu]);\n", top - 1); break;
        case OP_CALL: case OP_TAIL_CALL: {
            size_t call = top - in.arg - 1;
            int hint = call_hint(rt, fn, pc, in.arg);
            if (!scalar_builtin(hint)) {
                fprintf(out, "if(p[%zu]!=p[0]) goto scalar_fallback;\n", call);
                for (size_t i = 0; i < fn->arity; i++)
                    fprintf(out, "if(!value_is_int(p[%zu])) goto scalar_fallback;\n", call + i + 1);
                fprintf(out, "memmove(p+1,p+%zu,%zu*sizeof(Value));\n", call + 1, fn->arity);
                for (size_t i = fn->arity; i < fn->locals; i++) fprintf(out, "p[%zu]=V_UNBOUND;\n", i + 1);
                fputs("goto P0;\n", out);
                break;
            }
            fprintf(out, "if(p[%zu]!=%u) goto scalar_fallback;\n", call, ((unsigned)hint << 8) | TAG_BUILTIN);
            for (size_t i = 1; i <= in.arg; i++) fprintf(out, "if(!value_is_int(p[%zu])) goto scalar_fallback;\n", call + i);
            if (hint <= B_MUL) {
                const char *operation = hint == B_ADD ? "add" : hint == B_SUB ? "sub" : "mul";
                fprintf(out, "p[%zu]=p[%zu];\n", call, call + 1);
                for (size_t i = 2; i <= in.arg; i++) fprintf(out,
                    "if(!value_int_%s(p[%zu],p[%zu],&p[%zu])) return rt_error(rt,\"integer overflow\");\n",
                    operation, call, call + i, call);
            } else {
                const char *operation = hint == B_LT ? "<" : hint == B_LE ? "<=" : hint == B_GT ? ">" : hint == B_GE ? ">=" : "==";
                fprintf(out, "p[%zu]=(", call);
                for (size_t i = 2; i <= in.arg; i++) fprintf(out, "%s(value_to_int(p[%zu])%s value_to_int(p[%zu]))",
                    i == 2 ? "" : " && ", call + i - 1, operation, call + i);
                fputs(") ? V_TRUE:V_FALSE;\n", out);
            }
            if (in.op == OP_TAIL_CALL) fprintf(out, "return rt_leave(rt,p[%zu]);\n", call);
            break;
        }
        default: break;
        }
    }
    fputs("scalar_fallback: rt->native_scalar_fallbacks++;\n}\n", out);
}

/* Keep this body separate so its larger private values do not change register
 * allocation in the integer path. Two means replay the unchanged VM frame. */
static void emit_numeric(Runtime *rt, FILE *out, Function *fn, const Layout *layout) {
    if (!scalar_eligible(rt, fn, layout, true)) return;
    fputs("static __attribute__((noinline)) int otium_numeric(Runtime *rt, Value *s, size_t frame) {\n#if FLT_EVAL_METHOD == 0\n", out);
    size_t locals = fn->locals + 1;
    fputs("if(rt->frames[frame].ip==0", out);
    for (size_t i = 0; i < fn->arity; i++) fprintf(out, " && native_number(native_value(s[%zu]))", i + 1);
    for (size_t pc = 0; pc < fn->count; pc++) {
        Instruction in = fn->code[pc];
        if (in.op != OP_GLOBAL || layout->height[pc] == SIZE_MAX) continue;
        int id = rt_builtin_index(rt->names[in.arg]);
        if (scalar_builtin(id)) fprintf(out, " && rt->globals[%u]==%u", in.arg, ((unsigned)id << 8) | TAG_BUILTIN);
    }
    fputs(") {\nrt->native_scalar_entries++; rt->native_numeric_entries++;\n", out);
    for (size_t i = 0; i < locals + layout->maximum; i++) {
        fprintf(out, "NativeNumber p%zu", i);
        if (i < locals) fprintf(out, "=native_value(s[%zu])", i);
        fputs(";\n", out);
    }
    for (size_t pc = 0; pc < fn->count; pc++) {
        if (layout->height[pc] == SIZE_MAX) continue;
        size_t top = locals + layout->height[pc];
        Instruction in = fn->code[pc];
        fprintf(out, "Q%zu:;\n", pc);
        switch (in.op) {
        case OP_CONST:
            if (rt_kind(rt->constants[in.arg], K_FLOAT))
                fprintf(out, "p%zu=native_float_value(rt->constants[%u]);\n", top, in.arg);
            else fprintf(out, "p%zu=native_value((Value)UINT64_C(%" PRIu64 "));\n",
                top, (uint64_t)rt->constants[in.arg]);
            break;
        case OP_GLOBAL: {
            int id = rt_builtin_index(rt->names[in.arg]);
            if (scalar_builtin(id)) fprintf(out, "p%zu=native_value(%u);\n", top, ((unsigned)id << 8) | TAG_BUILTIN);
            else fprintf(out, "p%zu=native_value(rt->globals[%u]); if(p%zu.value==V_UNBOUND) goto numeric_fallback;\n", top, in.arg, top);
            break;
        }
        case OP_LOCAL:
            fprintf(out, "p%zu=p%u; if(p%zu.value==V_UNBOUND) goto numeric_fallback;\n", top, in.arg + 1, top); break;
        case OP_BIND: case OP_SET_LOCAL:
            fprintf(out, "p%u=p%zu;\n", in.arg + 1, top - 1); break;
        case OP_CLEAR_LOCAL: fprintf(out, "p%u=native_value(V_UNBOUND);\n", in.arg + 1); break;
        case OP_UPVALUE:
            fprintf(out, "p%zu=native_value(object_fields(object_fields(p0.value)[%u])[0]); if(p%zu.value==V_UNBOUND) goto numeric_fallback;\n",
                top, in.arg, top); break;
        case OP_POP: break;
        case OP_DUP: fprintf(out, "p%zu=p%zu;\n", top, top - 1); break;
        case OP_JUMP: fprintf(out, "goto Q%u;\n", in.arg); break;
        case OP_JFALSE:
            fprintf(out, "if(p%zu.value==V_FALSE || p%zu.value==V_NIL) goto Q%u;\n", top - 1, top - 1, in.arg); break;
        case OP_RETURN: fprintf(out, "return native_return(rt,p%zu);\n", top - 1); break;
        case OP_CALL: case OP_TAIL_CALL: {
            size_t call = top - in.arg - 1;
            int hint = call_hint(rt, fn, pc, in.arg);
            if (!scalar_builtin(hint)) {
                fprintf(out, "if(p%zu.value!=p0.value) goto numeric_fallback;\n", call);
                for (size_t i = 0; i < fn->arity; i++)
                    fprintf(out, "if(!native_number(p%zu)) goto numeric_fallback;\n", call + i + 1);
                for (size_t i = 0; i < fn->arity; i++)
                    fprintf(out, "p%zu=p%zu;\n", i + 1, call + i + 1);
                for (size_t i = fn->arity; i < fn->locals; i++) fprintf(out, "p%zu=native_value(V_UNBOUND);\n", i + 1);
                fputs("goto Q0;\n", out);
                break;
            }
            fprintf(out, "if(p%zu.value!=%u) goto numeric_fallback;\n", call, ((unsigned)hint << 8) | TAG_BUILTIN);
            for (size_t i = 1; i <= in.arg; i++) fprintf(out, "if(!native_number(p%zu)) goto numeric_fallback;\n", call + i);
            if (hint <= B_MUL) {
                /* Addition and multiplication include their identity operand,
                 * matching the VM's signed-zero and rounding behavior. */
                if (hint == B_SUB) fprintf(out, "p%zu=p%zu;\n", call, call + 1);
                else fprintf(out, "p%zu=native_value(value_int(%d));\n", call, hint == B_MUL);
                for (size_t i = hint == B_SUB ? 2 : 1; i <= in.arg; i++) fprintf(out,
                    "if(!native_arithmetic(%d,p%zu,p%zu,&p%zu)) return rt_error(rt,\"integer overflow\");\n",
                    hint, call, call + i, call);
            } else {
                fprintf(out, "p%zu=native_value((", call);
                for (size_t i = 2; i <= in.arg; i++) fprintf(out, "%snative_compare(%d,p%zu,p%zu)",
                    i == 2 ? "" : " && ", hint, call + i - 1, call + i);
                fputs(") ? V_TRUE:V_FALSE);\n", out);
            }
            if (in.op == OP_TAIL_CALL) fprintf(out, "return native_return(rt,p%zu);\n", call);
            break;
        }
        default: break;
        }
    }
    fputs("numeric_fallback: rt->native_scalar_fallbacks++; rt->native_numeric_fallbacks++;\n}\n#endif\nreturn 2;\n}\n", out);
}

static void roots(FILE *out, size_t height) {
    fprintf(out, "rt->sp=rt->stack_roots.count=base+%zu;\n", height);
}

static bool fast_call(FILE *out, int hint, size_t argc, size_t call, size_t height) {
    if (hint < 0) return false;
    if (argc >= 3 && argc <= 8 && (hint == B_ADD || hint == B_SUB || hint == B_MUL ||
            (hint >= B_LT && hint <= B_NUMEQ))) {
        fprintf(out, "if(v==%u && ((", ((unsigned)hint << 8) | TAG_BUILTIN);
        for (size_t i = 1; i <= argc; i++) fprintf(out, "%ss[%zu]", i == 1 ? "" : " & ", call + i);
        fputs(") & 1)) {\n", out);
        if (hint <= B_MUL) {
            const char *operation = hint == B_ADD ? "add" : hint == B_SUB ? "sub" : "mul";
            fprintf(out, "Value n=s[%zu],next;\n", call + 1);
            for (size_t i = 2; i <= argc; i++)
                fprintf(out, "if(!value_int_%s(n,s[%zu],&next))"
                    " return rt_error(rt,\"integer overflow\"); n=next;\n", operation, call + i);
            fputs("answer=n;\n", out);
        } else {
            const char *op = hint == B_LT ? "<" : hint == B_LE ? "<=" : hint == B_GT ? ">" : hint == B_GE ? ">=" : "==";
            fputs("answer=(", out);
            for (size_t i = 2; i <= argc; i++) fprintf(out, "%s(value_to_int(s[%zu])%s value_to_int(s[%zu]))",
                i == 2 ? "" : " && ", call + i - 1, op, call + i);
            fputs(") ? V_TRUE:V_FALSE;\n", out);
        }
    } else if (argc == 2 && (hint == B_ADD || hint == B_SUB || hint == B_MUL ||
            (hint >= B_LT && hint <= B_NUMEQ))) {
        fprintf(out, "if(v==%u && value_is_int(s[%zu]) && value_is_int(s[%zu])) {\n"
            "intptr_t a=value_to_int(s[%zu]),b=value_to_int(s[%zu]);\n",
            ((unsigned)hint << 8) | TAG_BUILTIN, call + 1, call + 2, call + 1, call + 2);
        if (hint <= B_MUL) {
            const char *operation = hint == B_ADD ? "add" : hint == B_SUB ? "sub" : "mul";
            fprintf(out, "intptr_t n; if(__builtin_%s_overflow(a,b,&n) || n<INTPTR_MIN/2 || n>INTPTR_MAX/2)"
                " return rt_error(rt,\"integer overflow\"); answer=value_int(n);\n", operation);
        } else {
            const char *op = hint == B_LT ? "<" : hint == B_LE ? "<=" : hint == B_GT ? ">" : hint == B_GE ? ">=" : "==";
            fprintf(out, "answer=a%s b ? V_TRUE:V_FALSE;\n", op);
        }
    } else if ((hint == B_CAR || hint == B_CDR) && argc == 1) {
        fprintf(out, "if(v==%u && rt_kind(s[%zu],K_PAIR)) { answer=object_fields(s[%zu])[%u];\n",
            ((unsigned)hint << 8) | TAG_BUILTIN, call + 1, call + 1, hint == B_CDR);
    } else if (hint == B_GET && (argc == 2 || argc == 3)) {
        fprintf(out, "if(v==%u && rt_kind(s[%zu],K_TABLE)) { answer=rt_table_get(rt,s[%zu],s[%zu],",
            ((unsigned)hint << 8) | TAG_BUILTIN, call + 1, call + 1, call + 2);
        if (argc == 3) fprintf(out, "s[%zu]", call + 3);
        else fputs("V_NIL", out);
        fputs("); if(!answer) return false;\n} else ", out);
        fprintf(out, "if(v==%u && rt_kind(s[%zu],K_ARRAY) && value_is_int(s[%zu]) && "
            "value_to_int(s[%zu])>=0 && (size_t)value_to_int(s[%zu])<((ArrayInfo*)object_bytes(s[%zu]))->length) {\n"
            "answer=object_fields(object_fields(s[%zu])[0])[(size_t)value_to_int(s[%zu])];\n",
            ((unsigned)hint << 8) | TAG_BUILTIN, call + 1, call + 2, call + 2, call + 2, call + 1, call + 1, call + 2);
    } else if (hint == B_PUT_MUT && argc == 3) {
        fprintf(out, "if(v==%u && rt_kind(s[%zu],K_TABLE)) {\n",
            ((unsigned)hint << 8) | TAG_BUILTIN, call + 1);
        roots(out, height);
        fprintf(out, "answer=rt_table_set(rt,s[%zu],s[%zu],s[%zu]); if(!answer) return false; s=rt->stack+base;\n",
            call + 1, call + 2, call + 3);
    } else if (hint == B_LENGTH && argc == 1) {
        fprintf(out, "if(v==%u && rt_kind(s[%zu],K_ARRAY)) { "
            "answer=value_int((intptr_t)((ArrayInfo*)object_bytes(s[%zu]))->length);\n",
            ((unsigned)hint << 8) | TAG_BUILTIN, call + 1, call + 1);
    } else if (hint == B_PUSH && argc == 2) {
        fprintf(out, "if(v==%u && rt_kind(s[%zu],K_ARRAY) && "
            "((ArrayInfo*)object_bytes(s[%zu]))->length<((ArrayInfo*)object_bytes(s[%zu]))->capacity) {\n"
            "ArrayInfo *a=object_bytes(s[%zu]); "
            "object_fields(object_fields(s[%zu])[0])[a->length++]=s[%zu]; answer=s[%zu];\n",
            ((unsigned)hint << 8) | TAG_BUILTIN, call + 1, call + 1, call + 1,
            call + 1, call + 1, call + 2, call + 1);
    } else if (hint == B_CONS && argc == 2) {
        fprintf(out, "if(v==%u) {\n", ((unsigned)hint << 8) | TAG_BUILTIN);
        roots(out, height);
        fputs("answer=rt_alloc(rt,K_PAIR,2,0); if(!answer) return false;\n", out);
        fprintf(out,
            "object_fields(answer)[0]=s[%zu]; object_fields(answer)[1]=s[%zu];\n", call + 1, call + 2);
    } else return false;
    fputs("} else ", out);
    return true;
}

static void emit_call(Runtime *rt, FILE *out, Function *fn, size_t pc, size_t height) {
    Instruction in = fn->code[pc];
    size_t call = height - in.arg - 1;
    fprintf(out, "v=s[%zu];\n", call);
    fast_call(out, call_hint(rt, fn, pc, in.arg), in.arg, call, height);
    fprintf(out, "if(v==%u) {\n", ((unsigned)B_APPLY << 8) | TAG_BUILTIN);
    roots(out, height);
    fprintf(out, "rt->frames[frame].ip=%zu; return rt_apply(rt,base+%zu,%u,%s);\n} else ",
        pc + 1, call, in.arg, in.op == OP_TAIL_CALL ? "true" : "false");
    fputs("if((v&255)==TAG_BUILTIN) {\n", out);
    roots(out, height);
    fprintf(out, "answer=rt_builtin(rt,(unsigned)(v>>8),base+%zu,%u);\n"
        "if(!answer || rt->error[0]) return false;\n", call + 1, in.arg);
    fputs("s=rt->stack+base;\n} else if(rt_kind(v,K_FOREIGN_FUNCTION)) {\n", out);
    roots(out, height);
    fprintf(out, "answer=rt_foreign_call(rt,base+%zu,base+%zu,%u);\n"
        "if(!answer || rt->error[0]) return false; s=rt->stack+base;\n} else {\n", call, call + 1, in.arg);
    fputs("if(rt_collection(v)) {\n", out);
    roots(out, height);
    fprintf(out, "answer=rt_collection_call(rt,base+%zu,base+%zu,%u);\n"
        "if(!answer || rt->error[0]) return false; s=rt->stack+base;\n", call, call + 1, in.arg);
    if (in.op == OP_TAIL_CALL) fputs("return rt_leave(rt,answer);\n", out);
    else fprintf(out, "s[%zu]=answer; goto L%zu;\n", call, pc + 1);
    fputs("}\n", out);
    bool unboxed = true;
    for (size_t i = 0; i < fn->arity; i++) if (fn->boxed[i]) unboxed = false;
    if (in.op == OP_TAIL_CALL && in.arg == fn->arity && unboxed && !fn->rest && !fn->optional) {
        fprintf(out, "if(v==s[0]) { memmove(s+1,s+%zu,%zu*sizeof(Value));\n", call + 1, fn->arity);
        for (size_t i = fn->arity; i < fn->locals; i++) fprintf(out, "s[%zu]=V_UNBOUND;\n", i + 1);
        fputs("goto L0; }\n", out);
    }
    if (in.op == OP_CALL && in.arg == fn->arity && unboxed && !fn->rest && !fn->optional) {
        fprintf(out, "if(v==s[0]) { size_t next=base+%zu;\n", call);
        fprintf(out, "if(next>rt->stack_limit || %zu>rt->stack_limit-next) return rt_error(rt,\"VM stack limit exceeded\");\n", 1 + fn->locals);
        roots(out, height);
        fprintf(out, "if(next+%zu>rt->stack_capacity && !rt_stack(rt,next+%zu)) return false;\n", 1 + fn->locals, 1 + fn->locals);
        fputs("if(rt->frame_count==rt->frame_capacity && !rt_grow(rt,(void**)&rt->frames,&rt->frame_capacity,rt->frame_count+1,sizeof(Frame))) return false;\n", out);
        fprintf(out, "rt->frames[frame].ip=%zu; rt->frames[rt->frame_count++]=(Frame){rt->frames[frame].function,0,next};\n", pc + 1);
        fputs("if(rt->frame_count>rt->max_frames) rt->max_frames=rt->frame_count;\n", out);
        for (size_t i = fn->arity; i < fn->locals; i++) fprintf(out, "rt->stack[next+%zu]=V_UNBOUND;\n", i + 1);
        fprintf(out, "rt->sp=rt->stack_roots.count=next+%zu;\n", 1 + fn->locals);
        fputs("if(rt->native_depth<NATIVE_DIRECT_LIMIT) {\n"
            "rt->native_depth++; if(rt->native_depth>rt->max_native_depth) rt->max_native_depth=rt->native_depth;\n"
            "rt->native_entries++; bool ok=otium_native_body(rt);\n"
            "if(ok && rt->frame_count>frame+1) ok=rt_drive(rt,frame+1);\n"
            "rt->native_depth--; if(!ok) return false; s=rt->stack+base;\n", out);
        fprintf(out, "goto L%zu; } return true; }\n", pc + 1);
    }
    roots(out, height);
    fprintf(out, "rt->frames[frame].ip=%zu; return rt_enter(rt,base+%zu,%u,%s);\n}\n",
        pc + 1, call, in.arg, in.op == OP_TAIL_CALL ? "true" : "false");
    if (in.op == OP_TAIL_CALL) fputs("return rt_leave(rt,answer);\n", out);
    else fprintf(out, "s[%zu]=answer; goto L%zu;\n", call, pc + 1);
}

static bool emit_function(Runtime *rt, FILE *out, Function *fn, const Layout *layout) {
    size_t local_end = 1 + fn->locals;
    fputs("#include \"native_numeric.h\"\n#include <string.h>\n", out);
    emit_numeric(rt, out, fn, layout);
    fputs(
        "static bool otium_native_body(Runtime *rt);\n"
        "bool otium_native_entry(Runtime *rt) { return otium_native_body(rt); }\n"
        "static bool otium_native_body(Runtime *rt) {\n"
        "size_t frame=rt->frame_count-1,base=rt->frames[frame].base;\n", out);
    fprintf(out, "if(base>rt->stack_limit || %zu>rt->stack_limit-base)"
        " return rt_error(rt,\"VM stack limit exceeded\");\n"
        "if(base+%zu>rt->stack_capacity && !rt_stack(rt,base+%zu)) return false;\n",
        local_end + layout->maximum, local_end + layout->maximum, local_end + layout->maximum);
    fputs("Value *s=rt->stack+base;\n", out);
    emit_scalar(rt, out, fn, layout);
    if (scalar_eligible(rt, fn, layout, true))
        fputs("if(rt->frames[frame].ip==0) { int result=otium_numeric(rt,s,frame); if(result!=2) return result; }\n", out);
    fputs("Value v=0,answer=0; (void)v; (void)answer;\n"
        "switch(rt->frames[frame].ip) {\n", out);
    for (size_t pc = 0; pc < fn->count; pc++)
        if (layout->height[pc] != SIZE_MAX) fprintf(out, "case %zu: goto L%zu;\n", pc, pc);
    fputs("default: return rt_error(rt,\"invalid native continuation\"); }\n", out);
    for (size_t pc = 0; pc < fn->count; pc++) {
        if (layout->height[pc] == SIZE_MAX) continue;
        size_t top = local_end + layout->height[pc];
        Instruction in = fn->code[pc];
        fprintf(out, "L%zu:;\n", pc);
        switch (in.op) {
        case OP_CONST:
            if (value_is_ptr(rt->constants[in.arg])) fprintf(out, "s[%zu]=rt->constants[%u];\n", top, in.arg);
            else fprintf(out, "s[%zu]=(Value)UINT64_C(%" PRIu64 ");\n", top, (uint64_t)rt->constants[in.arg]);
            break;
        case OP_GLOBAL:
            fprintf(out, "v=rt->globals[%u]; if(v==V_UNBOUND) return rt_error(rt,\"unbound global\"); s[%zu]=v;\n", in.arg, top);
            break;
        case OP_SET_GLOBAL:
            fprintf(out, "if(rt->globals[%u]==V_UNBOUND) return rt_error(rt,\"cannot set an unbound global\");\n", in.arg);
            /* fall through */
        case OP_DEFINE_GLOBAL:
            if (in.op == OP_DEFINE_GLOBAL) fprintf(out, "rt->global_defined[%u]=1;\n", in.arg);
            fprintf(out, "rt->globals[%u]=s[%zu];\n", in.arg, top - 1); break;
        case OP_LOCAL: case OP_PRESENT:
            fprintf(out, "v=s[%u];\n", in.arg + 1);
            if (fn->boxed[in.arg]) fputs("if(rt_kind(v,K_BOX)) v=object_fields(v)[0];\n", out);
            if (in.op == OP_PRESENT) fprintf(out, "s[%zu]=v==V_UNBOUND ? V_FALSE:V_TRUE;\n", top);
            else fprintf(out, "if(v==V_UNBOUND) return rt_error(rt,\"uninitialized local\"); s[%zu]=v;\n", top);
            break;
        case OP_CLEAR_LOCAL: fprintf(out, "s[%u]=V_UNBOUND;\n", in.arg + 1); break;
        case OP_BIND:
            if (fn->boxed[in.arg]) {
                roots(out, top);
                fputs("v=rt_alloc(rt,K_BOX,1,0); if(!v) return false;\n", out);
                fprintf(out, "object_fields(v)[0]=s[%zu]; s[%u]=v;\n", top - 1, in.arg + 1);
            } else fprintf(out, "s[%u]=s[%zu];\n", in.arg + 1, top - 1);
            break;
        case OP_SET_LOCAL:
            if (fn->boxed[in.arg]) fprintf(out,
                "v=s[%u]; if(!rt_kind(v,K_BOX)) return rt_error(rt,\"uninitialized captured binding\");"
                " object_fields(v)[0]=s[%zu];\n", in.arg + 1, top - 1);
            else fprintf(out, "s[%u]=s[%zu];\n", in.arg + 1, top - 1);
            break;
        case OP_UPVALUE:
            fprintf(out, "v=object_fields(object_fields(s[0])[%u])[0];"
                " if(v==V_UNBOUND) return rt_error(rt,\"uninitialized captured binding\"); s[%zu]=v;\n", in.arg, top);
            break;
        case OP_SET_UPVALUE:
            fprintf(out, "object_fields(object_fields(s[0])[%u])[0]=s[%zu];\n", in.arg, top - 1); break;
        case OP_CLOSURE: {
            Function *child = rt->functions[in.arg];
            roots(out, top);
            fprintf(out, "v=rt_alloc(rt,K_CLOSURE,%zu,sizeof(uint32_t)); if(!v) return false;\n", child->capture_count);
            fprintf(out, "{uint32_t id=%u; memcpy(object_bytes(v),&id,sizeof(id));}\n", in.arg);
            for (size_t i = 0; i < child->capture_count; i++) {
                Capture capture = child->captures[i];
                if (capture.local) fprintf(out, "object_fields(v)[%zu]=s[%u];\n", i, capture.index + 1);
                else fprintf(out, "object_fields(v)[%zu]=object_fields(s[0])[%u];\n", i, capture.index);
                fprintf(out, "if(!rt_kind(object_fields(v)[%zu],K_BOX)) return rt_error(rt,\"uninitialized captured binding\");\n", i);
            }
            fprintf(out, "s[%zu]=v;\n", top);
            break;
        }
        case OP_CALL: case OP_TAIL_CALL: emit_call(rt, out, fn, pc, top); continue;
        case OP_RETURN: fprintf(out, "return rt_leave(rt,s[%zu]);\n", top - 1); continue;
        case OP_POP: break;
        case OP_DUP: fprintf(out, "s[%zu]=s[%zu];\n", top, top - 1); break;
        case OP_JUMP: fprintf(out, "goto L%u;\n", in.arg); continue;
        case OP_JFALSE: fprintf(out, "if(!rt_truth(s[%zu])) goto L%u;\n", top - 1, in.arg); break;
        }
        fprintf(out, "goto L%zu;\n", pc + 1);
    }
    fputs("}\n", out);
    return !ferror(out);
}

typedef struct { Function *fn; Layout *layout; } NativeSource;

static bool emit_native(Runtime *rt, FILE *out, void *context) {
    NativeSource *source = context;
    return emit_function(rt, out, source->fn, source->layout);
}

bool rt_native_compile(Runtime *rt, uint32_t function) {
    Function *fn = rt->functions[function];
    Layout layout = {0};
    bool ok = analyze(rt, fn, &layout);
    if (ok) {
        NativeSource source = {fn, &layout};
        void *entry;
        ok = rt_module_compile(rt, emit_native, &source, "otium_native_entry",
            &fn->native_handle, &entry, &rt->native_compile_seconds);
        if (ok) {
            _Static_assert(sizeof(entry) == sizeof(fn->native), "POSIX function pointer size");
            memcpy(&fn->native, &entry, sizeof(entry));
            rt->native_functions++;
        }
    }
    free(layout.height);
    return ok;
}

void rt_native_close(Function *fn) {
    if (fn->native_handle) dlclose(fn->native_handle);
    fn->native_handle = NULL; fn->native = NULL;
}
