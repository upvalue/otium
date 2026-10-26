#include "internal.h"
#include <string.h>

static void height(Runtime *rt, size_t sp) {
    rt->sp = rt->stack_roots.count = sp;
}

static bool push(Runtime *rt, Value v) {
    if (!rt_stack(rt, rt->sp + 1)) return false;
    rt->stack[rt->sp] = v;
    height(rt, rt->sp + 1);
    return true;
}

bool rt_enter(Runtime *rt, size_t call, size_t argc, bool tail) {
    Value closure = rt->stack[call];
    if (!rt_kind(closure, K_CLOSURE)) return rt_error(rt, "value is not callable");
    uint32_t id;
    memcpy(&id, object_bytes(closure), sizeof(id));
    Function *fn = rt->functions[id];
    if (argc < fn->arity || (!fn->rest && argc > fn->arity + fn->optional)) return rt_error(rt, "wrong number of arguments");
    size_t base = tail ? rt->frames[rt->frame_count - 1].base : call;
    if (fn->locals >= rt->stack_limit || base >= rt->stack_limit - fn->locals)
        return rt_error(rt, "VM stack limit exceeded");
    if (base + 1 + fn->locals > rt->stack_capacity && !rt_stack(rt, base + 1 + fn->locals)) return false;
    if (!tail && rt->frame_count == rt->frame_capacity && !rt_grow(rt, (void **)&rt->frames, &rt->frame_capacity,
            rt->frame_count + 1, sizeof(Frame))) return false;
    size_t parameters = fn->arity + fn->optional + (fn->rest ? 1 : 0);
    Value rest = V_EMPTY;
    if (fn->rest && argc > fn->arity) {
        Roots root;
        roots_push(&rt->heap, &root, &rest, 1);
        for (size_t i = argc; i > fn->arity; i--) {
            Value pair = rt_alloc(rt, K_PAIR, 2, 0);
            if (!pair) { roots_pop(&rt->heap, &root); return false; }
            object_fields(pair)[0] = rt->stack[call + i];
            object_fields(pair)[1] = rest;
            rest = pair;
        }
        roots_pop(&rt->heap, &root);
        closure = rt->stack[call];
    }
    size_t supplied = fn->rest ? fn->arity : argc;
    if (base != call) memmove(rt->stack + base + 1, rt->stack + call + 1, supplied * sizeof(Value));
    rt->stack[base] = closure;
    for (size_t i = supplied; i < fn->locals; i++) rt->stack[base + 1 + i] = V_UNBOUND;
    if (fn->rest) rt->stack[base + 1 + fn->arity] = rest;
    height(rt, base + 1 + fn->locals);
    if (!tail) rt->frame_count++;
    rt->frames[rt->frame_count - 1] = (Frame){id, 0, base};
    if (rt->frame_count > rt->max_frames) rt->max_frames = rt->frame_count;
    for (size_t i = 0; i < parameters; i++) {
        if (!fn->boxed[i]) continue;
        Value box = rt_alloc(rt, K_BOX, 1, 0);
        if (!box) return false;
        object_fields(box)[0] = rt->stack[base + 1 + i];
        rt->stack[base + 1 + i] = box;
    }
    return true;
}

bool rt_leave(Runtime *rt, Value result) {
    size_t base = rt->frames[--rt->frame_count].base;
    /* The returning frame already owns this slot; no allocation is needed. */
    rt->stack[base] = result;
    height(rt, base + 1);
    return true;
}

bool rt_apply(Runtime *rt, size_t call, size_t argc, bool tail) {
    const Value apply = ((Value)B_APPLY << 8) | TAG_BUILTIN;
    while (rt->stack[call] == apply) {
        if (argc < 2) return rt_error(rt, "apply needs a callable and a final argument sequence");
        Value list = rt->stack[call + argc];
        bool array = rt_kind(list, K_ARRAY);
        Value cursor = array || list == V_NIL ? V_EMPTY : list, slow = cursor;
        size_t length = array ? ((ArrayInfo *)object_bytes(list))->length : 0, prefix = argc - 2;
        while (rt_kind(cursor, K_PAIR)) {
            if (length >= rt->stack_limit) return rt_error(rt, "VM stack limit exceeded");
            length++;
            cursor = object_fields(cursor)[1];
            if (!(length & 1)) slow = object_fields(slow)[1];
            if (cursor != V_EMPTY && cursor == slow) return rt_error(rt, "apply needs an acyclic proper list");
        }
        if (cursor != V_EMPTY) return rt_error(rt, "apply needs a proper list");
        if (length > rt->stack_limit || prefix > rt->stack_limit - length || call >= rt->stack_limit - prefix - length)
            return rt_error(rt, "VM stack limit exceeded");
        size_t count = prefix + length;
        if (call + 1 + count > rt->stack_capacity && !rt_stack(rt, call + 1 + count)) return false;
        memmove(rt->stack + call, rt->stack + call + 1, (prefix + 1) * sizeof(Value));
        if (array && length) {
            memcpy(rt->stack + call + 1 + prefix, object_fields(object_fields(list)[0]), length * sizeof(Value));
        } else for (size_t i = 0; i < length; i++) {
            rt->stack[call + 1 + prefix + i] = object_fields(list)[0];
            list = object_fields(list)[1];
        }
        argc = count;
        height(rt, call + 1 + argc);
    }
    Value target = rt->stack[call];
    if ((target & 255) == TAG_BUILTIN || rt_collection(target) || rt_kind(target, K_FOREIGN_FUNCTION)) {
        Value answer = rt_collection(target) ? rt_collection_call(rt, call, call + 1, argc)
            : rt_kind(target, K_FOREIGN_FUNCTION) ? rt_foreign_call(rt, call, call + 1, argc)
            : rt_builtin(rt, (unsigned)(target >> 8), call + 1, argc);
        if (!answer || rt->error[0]) return false;
        if (tail) return rt_leave(rt, answer);
        rt->stack[call] = answer;
        height(rt, call + 1);
        return true;
    }
    return rt_enter(rt, call, argc, tail);
}

bool rt_execute(Runtime *rt, uint32_t function, Value *result) {
    Value closure = rt_alloc(rt, K_CLOSURE, 0, sizeof(function));
    if (!closure) return false;
    memcpy(object_bytes(closure), &function, sizeof(function));
    if (!push(rt, closure)) return false;
    return rt_invoke(rt, 0, result);
}

bool rt_invoke(Runtime *rt, size_t argc, Value *result) {
    Value callable = rt->stack[0];
    if (callable == (((Value)B_APPLY << 8) | TAG_BUILTIN)) {
        if (!rt_apply(rt, 0, argc, false)) return false;
        goto dispatch;
    }
    if ((callable & 255) == TAG_BUILTIN || rt_kind(callable, K_FOREIGN_FUNCTION) || rt_collection(callable)) {
        Value answer = rt_collection(callable) ? rt_collection_call(rt, 0, 1, argc)
            : rt_kind(callable, K_FOREIGN_FUNCTION) ? rt_foreign_call(rt, 0, 1, argc)
            : rt_builtin(rt, (unsigned)(callable >> 8), 1, argc);
        if (!answer) return false;
        height(rt, 0);
        if (!push(rt, answer)) return false;
        *result = answer;
        return true;
    }
    if (!rt_enter(rt, 0, argc, false)) return false;
dispatch:;
    if (!rt_drive(rt, 0)) return false;
    *result = rt->stack[0];
    return true;
}

bool rt_drive(Runtime *rt, size_t stop_frames) {
    while (rt->frame_count > stop_frames && !rt->error[0]) {
        Frame *frame = &rt->frames[rt->frame_count - 1];
        Function *fn = rt->functions[frame->function];
        if (rt->native_threshold && fn->heat < rt->native_threshold) fn->heat++;
        if (rt->native_threshold && (fn->native || fn->heat >= rt->native_threshold)) {
            if (!fn->native && !rt_native_compile(rt, frame->function)) return false;
            rt->native_entries++;
            if (!fn->native(rt)) return false;
            continue;
        }
        Instruction ins = fn->code[frame->ip++];
        size_t base = frame->base;
        Value v;
        switch (ins.op) {
        case OP_CONST:
            if (!push(rt, rt->constants[ins.arg])) return false;
            break;
        case OP_GLOBAL:
            v = rt->globals[ins.arg];
            if (v == V_UNBOUND) return rt_error(rt, "unbound global");
            if (!push(rt, v)) return false;
            break;
        case OP_SET_GLOBAL:
            if (rt->globals[ins.arg] == V_UNBOUND) return rt_error(rt, "cannot set an unbound global");
            /* fall through */
        case OP_DEFINE_GLOBAL:
            if (ins.op == OP_DEFINE_GLOBAL) rt->global_defined[ins.arg] = 1;
            rt->globals[ins.arg] = rt->stack[rt->sp - 1];
            height(rt, rt->sp - 1);
            break;
        case OP_LOCAL: case OP_PRESENT:
            v = rt->stack[base + 1 + ins.arg];
            if (fn->boxed[ins.arg] && rt_kind(v, K_BOX)) v = object_fields(v)[0];
            if (ins.op == OP_PRESENT) v = v == V_UNBOUND ? V_FALSE : V_TRUE;
            else if (v == V_UNBOUND) return rt_error(rt, "uninitialized local");
            if (!push(rt, v)) return false;
            break;
        case OP_CLEAR_LOCAL:
            rt->stack[base + 1 + ins.arg] = V_UNBOUND;
            break;
        case OP_BIND:
            if (fn->boxed[ins.arg]) {
                v = rt_alloc(rt, K_BOX, 1, 0);
                if (!v) return false;
                object_fields(v)[0] = rt->stack[rt->sp - 1];
            } else v = rt->stack[rt->sp - 1];
            rt->stack[base + 1 + ins.arg] = v;
            height(rt, rt->sp - 1);
            break;
        case OP_SET_LOCAL:
            if (fn->boxed[ins.arg]) {
                Value box = rt->stack[base + 1 + ins.arg];
                if (!rt_kind(box, K_BOX)) return rt_error(rt, "uninitialized captured binding");
                object_fields(box)[0] = rt->stack[rt->sp - 1];
            }
            else rt->stack[base + 1 + ins.arg] = rt->stack[rt->sp - 1];
            height(rt, rt->sp - 1);
            break;
        case OP_UPVALUE:
            v = object_fields(object_fields(rt->stack[base])[ins.arg])[0];
            if (v == V_UNBOUND) return rt_error(rt, "uninitialized captured binding");
            if (!push(rt, v)) return false;
            break;
        case OP_SET_UPVALUE:
            object_fields(object_fields(rt->stack[base])[ins.arg])[0] = rt->stack[rt->sp - 1];
            height(rt, rt->sp - 1);
            break;
        case OP_CLOSURE: {
            Function *child = rt->functions[ins.arg];
            v = rt_alloc(rt, K_CLOSURE, (uint32_t)child->capture_count, sizeof(uint32_t));
            if (!v) return false;
            memcpy(object_bytes(v), &ins.arg, sizeof(ins.arg));
            for (size_t i = 0; i < child->capture_count; i++) {
                Capture capture = child->captures[i];
                object_fields(v)[i] = capture.local ? rt->stack[base + 1 + capture.index]
                    : object_fields(rt->stack[base])[capture.index];
                if (!rt_kind(object_fields(v)[i], K_BOX))
                    return rt_error(rt, "uninitialized captured binding");
            }
            if (!push(rt, v)) return false;
            break;
        }
        case OP_CALL: case OP_TAIL_CALL: {
            size_t call = rt->sp - ins.arg - 1;
            v = rt->stack[call];
            if (v == (((Value)B_APPLY << 8) | TAG_BUILTIN)) {
                if (!rt_apply(rt, call, ins.arg, ins.op == OP_TAIL_CALL)) return false;
                break;
            }
            if ((v & 255) == TAG_BUILTIN || rt_kind(v, K_FOREIGN_FUNCTION) || rt_collection(v)) {
                Value answer = rt_collection(v) ? rt_collection_call(rt, call, call + 1, ins.arg)
                    : rt_kind(v, K_FOREIGN_FUNCTION) ? rt_foreign_call(rt, call, call + 1, ins.arg)
                    : rt_builtin(rt, (unsigned)(v >> 8), call + 1, ins.arg);
                if (!answer || rt->error[0]) return false;
                if (ins.op == OP_TAIL_CALL) {
                    if (!rt_leave(rt, answer)) return false;
                } else {
                    height(rt, call);
                    if (!push(rt, answer)) return false;
                }
            } else if (!rt_enter(rt, call, ins.arg, ins.op == OP_TAIL_CALL)) return false;
            break;
        }
        case OP_RETURN:
            v = rt->stack[rt->sp - 1];
            if (!rt_leave(rt, v)) return false;
            break;
        case OP_POP: height(rt, rt->sp - 1); break;
        case OP_DUP:
            if (!push(rt, rt->stack[rt->sp - 1])) return false;
            break;
        case OP_JUMP: frame->ip = ins.arg; break;
        case OP_JFALSE:
            v = rt->stack[rt->sp - 1]; height(rt, rt->sp - 1);
            if (!rt_truth(v)) frame->ip = ins.arg;
            break;
        default: return rt_error(rt, "invalid bytecode");
        }
    }
    return !rt->error[0];
}
