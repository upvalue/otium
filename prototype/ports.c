#define _POSIX_C_SOURCE 200809L
#include "internal.h"
#include <errno.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
    size_t position, length, capacity, lookahead, offset;
    unsigned char peek[4];
    unsigned kind;
    bool input, closed, eof, busy;
    FILE *stream;
    char *host_buffer;
} Port;
static Port *info(Value p) { return object_bytes(p); }
void rt_port_repr(FILE *out, Value p) {
    if (info(p)->kind == 3 || info(p)->kind == 4) {
        fputs("#<port ",out); runtime_write(NULL,out,object_fields(p)[4]);
        fprintf(out," %s>",info(p)->input ? "input" : "output"); return;
    }
    const char *name = info(p)->kind == 0 ? "\"string\"" : info(p)->kind == 1 ? "\"bytevector\"" :
        info(p)->stream == stdin ? "stdin" : info(p)->stream == stdout ? "stdout" : "stderr";
    fprintf(out, "#<port %s %s>", name, info(p)->input ? "input" : "output");
}
static Value fail(Runtime *rt, const char *message) { rt_error(rt, message); return 0; }
static bool natural(Value v, size_t *n) {
    if (!value_is_int(v) || value_to_int(v) < 0) return false;
    *n = (size_t)value_to_int(v); return true;
}
static bool register_port(Runtime *rt, Value p) {
    if (!rt_grow(rt, (void **)&rt->open_ports, &rt->open_port_capacity, rt->open_port_count+1, sizeof(Value))) return false;
    rt->open_port_roots.slots = rt->open_ports;
    rt->open_ports[rt->open_port_count++] = p;
    rt->open_port_roots.count = rt->open_port_count;
    return true;
}
static void unregister_port(Runtime *rt, Value p) {
    for (size_t i=0; i<rt->open_port_count; i++) if (rt->open_ports[i] == p) {
        memmove(rt->open_ports+i,rt->open_ports+i+1,(rt->open_port_count-i-1)*sizeof(Value));
        rt->open_port_roots.count = --rt->open_port_count; return;
    }
}
static bool callable(Value v) {
    return (v&255)==TAG_BUILTIN || rt_kind(v,K_CLOSURE) || rt_kind(v,K_GENERIC) || rt_kind(v,K_NEXT) || rt_kind(v,K_INSTANCE) || rt_kind(v,K_RECORD) ||
        rt_kind(v,K_MACRO) || rt_kind(v,K_PARAM) || rt_kind(v,K_FOREIGN_FUNCTION) || rt_collection(v);
}
/* The caller roots *port. Never retain its opaque bytes across a callback. */
static Value callback(Runtime *rt, Value *port, size_t field, bool bytes, size_t start, size_t end) {
    Value function=object_fields(*port)[field];
    if (function==V_NIL) return V_NIL;
    if (info(*port)->busy) return fail(rt,"port callback reentry");
    Value v[]={function,V_EMPTY,object_fields(*port)[0],value_int((intptr_t)start),value_int((intptr_t)end)};
    Roots roots; roots_push(&rt->heap,&roots,v,5);
    bool ok=true;
    if(bytes) for(size_t i=3;i;i--) {
        Value pair=rt_alloc(rt,K_PAIR,2,0);
        if(!pair) { ok=false; break; }
        object_fields(pair)[0]=v[i+1]; object_fields(pair)[1]=v[1]; v[1]=pair;
    }
    Value result=0;
    if(ok) {
        info(*port)->busy=true;
        result=rt_call_list(rt,v[0],v[1]);
        info(*port)->busy=false;
    }
    roots_pop(&rt->heap,&roots); return result;
}
static void stream_error(Runtime *rt, Value *port, const char *op, int error) {
    if(!runtime_poll_interrupt(rt)) return;
    if(info(*port)->kind==4) rt_file_error(rt,op,object_fields(*port)[4],error ? error : EIO);
    else rt_io_error(rt,":other");
}
static bool flush(Runtime *rt, Value *port) {
    if(info(*port)->kind!=3) {
        if(info(*port)->stream && fflush(info(*port)->stream)) { stream_error(rt,port,"flush-output-port",errno); return false; }
        return true;
    }
    while(info(*port)->offset<info(*port)->length) {
        size_t left=info(*port)->length-info(*port)->offset;
        Value n=callback(rt,port,1,true,info(*port)->offset,info(*port)->length);
        if(!n) return false;
        size_t written;
        if(!natural(n,&written) || !written || written>left) return rt_error(rt,"invalid custom port write count");
        info(*port)->offset+=written;
    }
    info(*port)->offset=info(*port)->length=0;
    return callback(rt,port,2,false,0,0)!=0;
}
static Value make_custom(Runtime *rt, Value *port, size_t first, size_t count, bool input) {
    if(count!=2 || !rt_kind(rt->stack[first],K_STRING) || !rt_kind(rt->stack[first+1],K_TABLE))
        return fail(rt,"custom port needs a name and callback table");
    Value callbacks[]={V_NIL,V_NIL,V_NIL}; Roots roots;
    roots_push(&rt->heap,&roots,callbacks,3);
    const char *names[]={input ? ":read" : ":write",":flush",":close"};
    bool ok=true;
    for(size_t i=0;i<3;i++) {
        Value key=rt_intern(rt,K_KEYWORD,names[i],strlen(names[i]));
        if(!key) { ok=false; break; }
        callbacks[i]=rt_table_get(rt,rt->stack[first+1],key,V_NIL);
        if(!callbacks[i]) { ok=false; break; }
        if((!i || callbacks[i]!=V_NIL) && !callable(callbacks[i])) { fail(rt,"invalid custom port callback"); ok=false; break; }
    }
    if(ok) {
        *port=rt_alloc(rt,K_PORT,5,sizeof(Port)); ok=*port!=0;
        if(ok) {
            info(*port)->input=input; info(*port)->kind=3; info(*port)->capacity=4096;
            for(size_t i=0;i<3;i++) object_fields(*port)[i+1]=callbacks[i];
            object_fields(*port)[4]=rt->stack[first];
            Value buffer=rt_bytes_new(rt,4096);
            ok=buffer!=0;
            if(ok) { object_fields(*port)[0]=buffer; ok=register_port(rt,*port); }
        }
    }
    roots_pop(&rt->heap,&roots); return ok ? *port : 0;
}
static Value open_file(Runtime *rt, Value *port, size_t first, size_t count, bool input) {
    if(count<1 || count>2 || !rt_kind(rt->stack[first],K_STRING) ||
       (count==2 && !rt_kind(rt->stack[first+1],K_TABLE)))
        return fail(rt,"file port needs a path and optional options table");
    size_t size=4096; int flags=input ? O_RDONLY : O_WRONLY|O_CREAT|O_TRUNC;
    const char *mode=input ? "rb" : "wb";
    if(count==2) {
        Value key=rt_intern(rt,K_KEYWORD,":buffer-size",12); if(!key) return 0;
        Value option=rt_table_get(rt,rt->stack[first+1],key,V_NIL); if(!option) return 0;
        if(option!=V_NIL && (!natural(option,&size) || !size)) return fail(rt,"invalid port buffer size");
        if(!input) {
            key=rt_intern(rt,K_KEYWORD,":if-exists",10); if(!key) return 0;
            option=rt_table_get(rt,rt->stack[first+1],key,V_NIL); if(!option) return 0;
            if(option!=V_NIL) {
                if(!rt_kind(option,K_KEYWORD)) return fail(rt,"invalid if-exists option");
                const char *name=(char *)rt_bytes_data(option);
                if(!strcmp(name,":append")) { flags=O_WRONLY|O_CREAT|O_APPEND; mode="ab"; }
                else if(!strcmp(name,":error")) flags=O_WRONLY|O_CREAT|O_EXCL;
                else if(strcmp(name,":truncate")) return fail(rt,"invalid if-exists option");
            }
        }
    }
    if(size<4) size=4;
    size_t length=rt_bytes_length(rt->stack[first]);
    if(memchr(rt_bytes_data(rt->stack[first]),0,length)) return fail(rt,"file path contains NUL");
    *port=rt_alloc(rt,K_PORT,5,sizeof(Port)); if(!*port) return 0;
    object_fields(*port)[4]=rt->stack[first];
    char *buffer=malloc(size); if(!buffer) return fail(rt,"out of host memory");
    int fd=open((char *)rt_bytes_data(rt->stack[first]),flags,0666);
    int error=errno; struct stat st;
    if(fd>=0) {
        if(fstat(fd,&st)) error=errno;
        else if(S_ISDIR(st.st_mode)) error=EISDIR;
        else error=0;
    }
    FILE *stream=NULL;
    if(fd>=0 && !error) { stream=fdopen(fd,mode); if(!stream) error=errno; }
    if(!stream) {
        if(fd>=0) close(fd);
        free(buffer); return rt_file_error(rt,input ? "open-input-file" : "open-output-file",object_fields(*port)[4],error);
    }
    if(setvbuf(stream,buffer,_IOFBF,size)) {
        fclose(stream); free(buffer); return fail(rt,"cannot configure file buffer");
    }
    info(*port)->kind=4; info(*port)->input=input;
    info(*port)->stream=stream; info(*port)->host_buffer=buffer;
    if(!register_port(rt,*port)) {
        fclose(stream); free(buffer); info(*port)->stream=NULL; info(*port)->host_buffer=NULL; return 0;
    }
    return *port;
}
void rt_ports_destroy(Runtime *rt) {
    for(size_t i=0;i<rt->open_port_count;i++) {
        Port *p=info(rt->open_ports[i]);
        if(p->kind==4 && p->stream) { fclose(p->stream); free(p->host_buffer); p->stream=NULL; p->host_buffer=NULL; }
    }
}
static Value standard(Runtime *rt, unsigned which) {
    if (rt->standard_ports[which] == V_NIL) {
        Value p = rt_alloc(rt, K_PORT, 5, sizeof(Port)); if (!p) return 0;
        info(p)->kind = 2; info(p)->input = which == 0;
        info(p)->stream = which == 0 ? stdin : which == 1 ? stdout : stderr;
        rt->standard_ports[which] = p;
        if (!register_port(rt,p)) return 0;
    }
    return rt->standard_ports[which];
}
static Value current(Runtime *rt, bool input) {
    const char *name = input ? "current-input-port" : "current-output-port";
    uint32_t index = rt_binding_global(rt, 0, name, strlen(name), false);
    if (index == UINT32_MAX || rt->globals[index] == V_UNBOUND) return standard(rt, input ? 0 : 1);
    Value p = rt->globals[index];
    if (!rt_kind(p, K_PARAM)) return fail(rt, "current port binding must be a param");
    return rt_param_call(rt, p, 0);
}
static bool check(Runtime *rt, Value p, bool input) {
    if (!rt_kind(p, K_PORT) || info(p)->input != input) return rt_error(rt, input ? "expected input port" : "expected output port");
    if (info(p)->busy) return rt_error(rt, "port callback reentry");
    if (info(p)->closed) { rt_io_error(rt, ":closed"); return false; }
    return true;
}
static int peek(Runtime *rt, Value *port, size_t ahead) {
    Port *s = info((*port));
    if(s->kind==3) {
        while(info(*port)->length-info(*port)->offset<=ahead) {
            s=info(*port);
            if(s->eof) return -1;
            size_t remaining=s->length-s->offset;
            unsigned char *data=rt_bytes_data(object_fields(*port)[0]);
            memmove(data,data+s->offset,remaining);
            s->offset=0; s->length=remaining;
            size_t available=s->capacity-remaining;
            Value result=callback(rt,port,1,true,remaining,s->capacity);
            if(!result) return -2;
            if(result==V_EOF) { info(*port)->eof=true; return -1; }
            size_t n;
            if(!natural(result,&n) || !n || n>available) { fail(rt,"invalid custom port read count"); return -2; }
            info(*port)->length+=n;
        }
        return rt_bytes_data(object_fields(*port)[0])[info(*port)->offset+ahead];
    }
    if (!s->stream) return ahead < s->length-s->position ? rt_bytes_data(object_fields((*port))[0])[s->position+ahead] : -1;
    while (s->lookahead <= ahead) {
        if (!runtime_poll_interrupt(rt)) return -2;
        errno = 0; int c = fgetc(s->stream);
        if (c == EOF) {
            if (ferror(s->stream)) {
                int error=errno; clearerr(s->stream);
                stream_error(rt,port,"read",error);
                return -2;
            }
            return -1;
        }
        s->peek[s->lookahead++] = (unsigned char)c;
    }
    return s->peek[ahead];
}
static void consume(Value p, size_t n) {
    Port *s = info(p); s->position += n;
    if(s->kind==3) s->offset+=n;
    if (s->stream) { s->lookahead -= n; memmove(s->peek, s->peek+n, s->lookahead); }
}
static int character(Runtime *rt, Value *port, uint32_t *cp, size_t *width) {
    int first = peek(rt, port, 0); if (first < 0) return first;
    unsigned char bytes[4]; bytes[0] = (unsigned char)first;
    *width = first < 128 ? 1 : first >= 0xc2 && first <= 0xdf ? 2 : first >= 0xe0 && first <= 0xef ? 3 : first >= 0xf0 && first <= 0xf4 ? 4 : 0;
    bool valid = *width != 0;
    for (size_t i=1; valid && i<*width; i++) {
        int b = peek(rt, port, i); if (b == -2) return -2;
        if (b < 0) valid = false; else bytes[i] = (unsigned char)b;
    }
    if (!valid || !rt_utf8_scalar(bytes, 0, *width)) { rt_decode_error(rt, info((*port))->position); return -2; }
    rt_utf8_next((char *)bytes, *width, 0, cp); return 0;
}
static bool datum_space(uint32_t c) {
    return c == ' ' || c == '\t' || c == '\r' || c == '\n' || c == '\f' || c == '\v';
}
static bool datum_delimiter(uint32_t c) {
    return datum_space(c) || (c < 128 && strchr("()[]{}\";'`,", (int)c));
}
/* Only establish the boundary here; the shared reader validates the syntax. */
static Value read_datum(Runtime *rt, Value *port) {
    enum { NORMAL, ATOM, STRING, ESCAPE, COMMENT, CHAR_FIRST, CHAR_TOKEN } mode = NORMAL;
    size_t depth = 0, length = 0, capacity = 0, token = 0;
    char closers[512];
    bool started = false, comma = false, done = false;
    char *source = NULL;
    while (!done) {
        if (!runtime_poll_interrupt(rt)) { free(source); return 0; }
        uint32_t cp; size_t width;
        int state = character(rt, port, &cp, &width);
        if (state == -2) { free(source); return 0; }
        if (state == -1) break;
        bool delim = datum_delimiter(cp);
        if (mode == ATOM) {
            if (length-token == 1 && source[token] == '#' && cp == '\\') mode = CHAR_FIRST;
            else if (length-token == 1 && source[token] == '#' && cp == '`') mode = NORMAL;
            else if (length-token == 3 && !memcmp(source+token, "#u8", 3) && cp == '(') {
                if (depth < sizeof(closers)) closers[depth] = ')';
                depth++; mode = NORMAL;
            } else if (delim) {
                if (!depth) break;
                mode = NORMAL; continue;
            }
        } else if (mode == CHAR_FIRST) {
            mode = delim ? NORMAL : CHAR_TOKEN;
            if (delim && !depth) done = true;
        } else if (mode == CHAR_TOKEN) {
            if (delim) {
                if (!depth) break;
                mode = NORMAL; continue;
            }
        } else if (mode == COMMENT) {
            if (cp == '\n' || cp == '\r') mode = NORMAL;
        } else if (mode == ESCAPE) mode = STRING;
        else if (mode == STRING) {
            if (cp == '\\') mode = ESCAPE;
            else if (cp == '"') { mode = NORMAL; if (!depth) done = true; }
        } else {
            if (comma && cp == '@') comma = false;
            else {
                comma = false;
                if (cp == ';') mode = COMMENT;
                else if (!datum_space(cp)) {
                    started = true;
                    if (cp == '(' || cp == '[' || cp == '{') {
                        if (depth < sizeof(closers)) closers[depth] = cp == '(' ? ')' : cp == '[' ? ']' : '}';
                        depth++;
                    }
                    else if (cp == ')' || cp == ']' || cp == '}') {
                        if (!depth || closers[depth-1] != (char)cp) done = true;
                        else if (!--depth) done = true;
                    } else if (cp == '"') mode = STRING;
                    else if (cp == ',' || cp == '\'' || cp == '`') comma = cp == ',';
                    else { token = length; mode = ATOM; }
                }
            }
        }
        if (depth > 512 || length > 16*1024*1024-width) {
            free(source); return fail(rt, "port datum exceeds reader limit");
        }
        if (!rt_grow(rt, (void **)&source, &capacity, length+width, 1)) { free(source); return 0; }
        char encoded[4]; size_t used = rt_utf8_encode(cp, encoded);
        memcpy(source+length, encoded, used); length += used;
        consume((*port), width);
    }
    Value result = started ? rt_read_source(rt, source, length) : V_EOF;
    free(source); return result;
}
static bool append(Runtime *rt, Value *port, const char *bytes, size_t n) {
    if (!check(rt, (*port), false)) return false;
    if(info(*port)->kind==3) {
        while(n) {
            if(info(*port)->length==info(*port)->capacity && !flush(rt,port)) return false;
            size_t room=info(*port)->capacity-info(*port)->length;
            size_t take=n<room ? n : room;
            memcpy(rt_bytes_data(object_fields(*port)[0])+info(*port)->length,bytes,take);
            info(*port)->length+=take; bytes+=take; n-=take;
            if(info(*port)->length==info(*port)->capacity && !flush(rt,port)) return false;
        }
        return true;
    }
    if (info((*port))->stream) {
        bool ok = fwrite(bytes, 1, n, info((*port))->stream) == n && (info(*port)->kind!=2 || !fflush(info((*port))->stream));
        if (!ok) stream_error(rt,port,"write",errno);
        return ok;
    }
    size_t length = info((*port))->length;
    if (n > SIZE_MAX-length) return rt_error(rt, "port output too large");
    if (length+n > info((*port))->capacity) {
        size_t capacity = info((*port))->capacity;
        if (capacity > SIZE_MAX/2) capacity = length+n;
        else capacity *= 2;
        if (capacity < 64) capacity = 64;
        if (capacity < length+n) capacity = length+n;
        Value data = rt_alloc(rt, K_BUFFER_DATA, 0, capacity);
        if (data) {
            if (length) memcpy(object_bytes(data), object_bytes(object_fields((*port))[0]), length);
            object_fields((*port))[0] = data; info((*port))->capacity = capacity;
        }
        if (!data) return false;
    }
    if (n) memcpy((char *)object_bytes(object_fields((*port))[0])+length, bytes, n);
    info((*port))->length += n; return true;
}
Value rt_port_print(Runtime *rt, unsigned id, size_t first, size_t count) {
    Value p;
    if (id == B_DISPLAY_TO || id == B_WRITE_TO) {
        if (!count) return fail(rt, "output needs a port");
        p = rt->stack[first++]; count--;
    } else if (id == B_NEWLINE) {
        if (count > 1) return fail(rt, "newline takes an optional port");
        p = count ? rt->stack[first] : current(rt, false); count = 0;
    } else p = current(rt, false);
    if (!p || !check(rt, p, false)) return 0;
    char *bytes = NULL; size_t length = 0;
    FILE *stream = open_memstream(&bytes, &length);
    if (!stream) return fail(rt, "out of host memory");
    for (size_t i=0; i<count; i++) {
        if (i) fputc(' ', stream);
        if (id == B_WRITE || id == B_WRITE_TO) runtime_write(rt, stream, rt->stack[first+i]);
        else runtime_print(rt, stream, rt->stack[first+i]);
    }
    if (id == B_PRINTLN || id == B_NEWLINE) fputc('\n', stream);
    bool ok = !ferror(stream); if (fclose(stream)) ok = false;
    Roots root; roots_push(&rt->heap, &root, &p, 1);
    if (ok) ok = append(rt, &p, bytes, length); else rt_error(rt, "cannot format port output");
    roots_pop(&rt->heap, &root);
    free(bytes); return ok ? V_NIL : 0;
}
static bool range(Runtime *rt, size_t first, size_t count, size_t n, size_t *start, size_t *end) {
    *start=0; *end=n;
    if ((count && !natural(rt->stack[first], start)) || (count>1 && !natural(rt->stack[first+1], end)) || *start>*end || *end>n)
        return rt_error(rt, "invalid port slice");
    return true;
}
static Value port_builtin(Runtime *rt, unsigned id, size_t first, size_t count, Value live[2]) {
    if(id>=B_OPEN_INPUT_FILE) return open_file(rt,&live[0],first,count,id==B_OPEN_INPUT_FILE || id==B_OPEN_BINARY_INPUT);
    if(id==B_MAKE_INPUT_PORT || id==B_MAKE_OUTPUT_PORT) return make_custom(rt,&live[0],first,count,id==B_MAKE_INPUT_PORT);
    if(id==B_OPEN_PORTS) {
        if(count) return fail(rt,"open-ports takes no arguments");
        size_t base=rt->sp, n=rt->open_port_count;
        if(n>rt->stack_limit-base || !rt_stack(rt,base+n)) return fail(rt,"VM stack limit exceeded");
        if(n) memcpy(rt->stack+base,rt->open_ports,n*sizeof(Value));
        rt->sp=rt->stack_roots.count=base+n;
        Value result=rt_collection_builtin(rt,B_ARRAY,base,n);
        rt->sp=rt->stack_roots.count=base; return result;
    }
    if (id == B_DISPLAY_TO || id == B_WRITE_TO) return rt_port_print(rt,id,first,count);
    if (id >= B_STDIN) { if (count) return fail(rt,"standard port takes no arguments"); return standard(rt,id-B_STDIN); }
    if (id <= B_OUTPUT_OPEN) {
        if (count != 1) return fail(rt,"port predicate takes one argument");
        live[0]=rt->stack[first]; bool yes=rt_kind(live[0],K_PORT);
        if (yes && id != B_PORT_P) yes = info(live[0])->input == (id==B_INPUT_P || id==B_INPUT_OPEN);
        if (yes && (id==B_INPUT_OPEN || id==B_OUTPUT_OPEN)) yes = !info(live[0])->closed;
        return yes ? V_TRUE : V_FALSE;
    }
    if (id==B_OPEN_INPUT_STRING || id==B_OPEN_INPUT_BYTES || id==B_OPEN_OUTPUT_STRING || id==B_OPEN_OUTPUT_BYTES) {
        bool input=id==B_OPEN_INPUT_STRING || id==B_OPEN_INPUT_BYTES;
        bool string=id==B_OPEN_INPUT_STRING || id==B_OPEN_OUTPUT_STRING;
        if (count != (input ? 1u : 0u)) return fail(rt,"wrong number of port constructor arguments");
        size_t n=0;
        if (input) { if (!rt_kind(rt->stack[first],string ? K_STRING : K_BYTEVECTOR)) return fail(rt,"invalid input source"); n=rt_bytes_length(rt->stack[first]); }
        live[0]=rt_alloc(rt,K_PORT,5,sizeof(Port)); if (!live[0]) return 0;
        info(live[0])->input=input; info(live[0])->kind=string ? 0 : 1; info(live[0])->length=n;
        if (input) {
            Value bytes=rt_bytes_new(rt,n);
            if (bytes) { if(n) memcpy(rt_bytes_data(bytes),rt_bytes_data(rt->stack[first]),n); object_fields(live[0])[0]=bytes; }
            if(!bytes) return 0;
        }
        return live[0];
    }
    if (id==B_CLOSE_PORT || id==B_CLOSE_INPUT || id==B_CLOSE_OUTPUT) {
        if(count!=1 || !rt_kind(rt->stack[first],K_PORT)) return fail(rt,"close needs a port");
        live[0]=rt->stack[first];
        if ((id==B_CLOSE_INPUT && !info(live[0])->input) || (id==B_CLOSE_OUTPUT && info(live[0])->input)) return fail(rt,"wrong port direction");
        if(info(live[0])->closed) return V_NIL;
        if(info(live[0])->busy) return fail(rt,"port callback reentry");
        if(!info(live[0])->input && !flush(rt,&live[0])) return 0;
        if(info(live[0])->kind==3 && !callback(rt,&live[0],3,false,0,0)) return 0;
        int error=0;
        if(info(live[0])->kind==4) {
            if(fclose(info(live[0])->stream)) error=errno;
            free(info(live[0])->host_buffer); info(live[0])->host_buffer=NULL; info(live[0])->stream=NULL;
        }
        info(live[0])->closed=true; unregister_port(rt,live[0]);
        return error ? rt_file_error(rt,"close-port",object_fields(live[0])[4],error) : V_NIL;
    }
    if (id==B_GET_OUTPUT_STRING || id==B_GET_OUTPUT_BYTES) {
        if(count!=1) return fail(rt,"output extraction needs a port");
        live[0]=rt->stack[first]; if(!check(rt,live[0],false)) return 0;
        if(info(live[0])->kind != (id==B_GET_OUTPUT_STRING ? 0u : 1u)) return fail(rt,"wrong output port kind");
        size_t n=info(live[0])->length;
        char *copy=malloc(n+1); if(!copy) return fail(rt,"out of host memory");
        if(n) memcpy(copy,object_bytes(object_fields(live[0])[0]),n);
        Value result;
        if(id==B_GET_OUTPUT_STRING) {
            for(size_t i=0;i<n;) { size_t next=rt_utf8_scalar((unsigned char *)copy,i,n); if(!next) { free(copy); return rt_decode_error(rt,i); } i=next; }
            result=rt_text(rt,K_STRING,copy,n);
        } else { result=rt_bytes_new(rt,n); if(result && n) memcpy(rt_bytes_data(result),copy,n); }
        free(copy); return result;
    }
    if (id == B_READ_DATUM) {
        if (count > 1) return fail(rt, "read takes an optional port");
        live[0]= count ? rt->stack[first] : current(rt, true);
        if (!live[0] || !check(rt, live[0], true)) return 0;
        return read_datum(rt, &live[0]);
    }
    bool output=id>=B_WRITE_U8 && id<=B_FLUSH_PORT;
    bool payload=(id>=B_READ_CHARACTERS && id<=B_READ_BYTES_MUT) || (id>=B_WRITE_U8 && id<=B_WRITE_BYTES) || id>=B_READ_UNTIL;
    size_t min=payload ? 1 : 0, max=min+1;
    if(id==B_READ_BYTES_MUT || id==B_WRITE_STRING || id==B_WRITE_BYTES) max=4;
    if(count<min || count>max) return fail(rt,"wrong number of port arguments");
    live[0]=count>min ? rt->stack[first+min] : current(rt,!output);
    if(!live[0] || !check(rt,live[0],!output)) return 0;
    live[1]=payload ? rt->stack[first] : V_NIL;
    if(output) {
        if(id==B_FLUSH_PORT) {
            return flush(rt,&live[0]) ? V_NIL : 0;
        }
        char one[4]; const char *source=one; size_t n=1;
        if(id==B_WRITE_U8) {
            size_t byte; if(!natural(live[1],&byte) || byte>255) return fail(rt,"expected byte"); one[0]=(char)byte;
        } else if(id==B_WRITE_CHAR) {
            if((live[1]&255)!=TAG_CHAR) return fail(rt,"expected character");
            n=rt_utf8_encode((uint32_t)(live[1]>>8),one);
        } else {
            bool string=id==B_WRITE_STRING;
            if(!rt_kind(live[1],string ? K_STRING : K_BYTEVECTOR)) return fail(rt,"invalid output data");
            size_t length=rt_bytes_length(live[1]), items=length;
            source=(char *)rt_bytes_data(live[1]);
            if(string) { items=0; for(size_t i=0;i<length;items++) { uint32_t cp; i=rt_utf8_next(source,length,i,&cp); } }
            size_t start,end; if(!range(rt,first+2,count>2 ? count-2 : 0,items,&start,&end)) return 0;
            if(string) { size_t lo=0,hi; uint32_t cp; for(size_t i=0;i<start;i++) lo=rt_utf8_next(source,length,lo,&cp); hi=lo; for(size_t i=start;i<end;i++) hi=rt_utf8_next(source,length,hi,&cp); start=lo;end=hi; }
            source+=start; n=end-start;
        }
        char *copy=malloc(n+1); if(!copy) return fail(rt,"out of host memory");
        memcpy(copy,source,n); bool ok=append(rt,&live[0],copy,n); free(copy); return ok ? V_NIL : 0;
    }
    if(id==B_READ_U8 || id==B_PEEK_U8) {
        int c=peek(rt,&live[0],0); if(c==-2) return 0; if(c==-1) return V_EOF;
        if(id==B_READ_U8) consume(live[0],1);
        return value_int(c);
    }
    if(id==B_READ_CHAR || id==B_PEEK_CHAR) {
        uint32_t cp; size_t width; int result=character(rt,&live[0],&cp,&width);
        if(result<0) return result==-1 ? V_EOF : 0;
        if(id==B_READ_CHAR) consume(live[0],width);
        return ((Value)cp<<8)|TAG_CHAR;
    }
    size_t limit=SIZE_MAX,start=0,end=0;
    if(id==B_READ_CHARACTERS || id==B_READ_BYTES) { if(!natural(live[1],&limit)) return fail(rt,"expected nonnegative count"); }
    if(id==B_READ_BYTES_MUT) {
        if(!rt_kind(live[1],K_BYTEVECTOR)) return fail(rt,"expected bytevector");
        if(!range(rt,first+2,count>2 ? count-2 : 0,rt_bytes_length(live[1]),&start,&end)) return 0;
        limit=end-start;
    }
    bool scan=id>=B_READ_UNTIL;
    if(scan && !rt_kind(live[1],K_STRING)) return fail(rt,"expected character set");
    char *bytes=NULL; size_t n=0,capacity=0,items=0;
    bool binary=id==B_READ_BYTES || id==B_READ_BYTES_MUT;
    while(items<limit) {
        if(!runtime_poll_interrupt(rt)) { free(bytes); return 0; }
        uint32_t cp=0; size_t width=1;
        int result=binary ? peek(rt,&live[0],0) : character(rt,&live[0],&cp,&width);
        if(result==-2) { free(bytes); return 0; } if(result==-1) break;
        if(binary) cp=(uint32_t)result;
        if(scan) {
            bool found=false; size_t len=rt_bytes_length(live[1]); const char *set=(char *)rt_bytes_data(live[1]);
            for(size_t i=0;i<len;) { uint32_t c; i=rt_utf8_next(set,len,i,&c); if(c==cp) found=true; }
            if(found==(id==B_READ_UNTIL)) break;
        }
        consume(live[0],width); items++;
        if(id==B_READ_LINE && cp=='\n') { if(n && bytes[n-1]=='\r') n--; break; }
        if(id==B_READ_BYTES_MUT) { rt_bytes_data(live[1])[start+items-1]=(unsigned char)cp; continue; }
        if(id==B_SKIP_WHILE) continue;
        char encoded[4]; size_t used=binary ? 1 : rt_utf8_encode(cp,encoded); if(binary) encoded[0]=(char)cp;
        if(!rt_grow(rt,(void **)&bytes,&capacity,n+used,1)) { free(bytes); return 0; }
        memcpy(bytes+n,encoded,used); n+=used;
    }
    if(id==B_READ_BYTES_MUT) return items || !limit ? value_int((intptr_t)items) : V_EOF;
    if(id==B_SKIP_WHILE) return value_int((intptr_t)items);
    if(!items && limit && (id==B_READ_LINE || id==B_READ_CHARACTERS || id==B_READ_BYTES)) { free(bytes); return V_EOF; }
    Value result;
    if(id==B_READ_BYTES) { result=rt_bytes_new(rt,n); if(result && n) memcpy(rt_bytes_data(result),bytes,n); }
    else result=rt_text(rt,K_STRING,bytes ? bytes : "",n);
    free(bytes); return result;
}

Value rt_port_builtin(Runtime *rt, unsigned id, size_t first, size_t count) {
    Value live[2] = {V_NIL, V_NIL}; Roots roots;
    roots_push(&rt->heap, &roots, live, 2);
    Value result = port_builtin(rt, id, first, count, live);
    roots_pop(&rt->heap, &roots); return result;
}

bool runtime_flush_ports(Runtime *rt) {
    if(rt->frame_count || rt->evaluating || rt->compiling || rt->eval_depth)
        return rt_error(rt,"port flushing requires an idle runtime");
    Value saved[]={rt->condition_values[0],rt->restart_transfer[0],rt->restart_transfer[1]};
    Roots state_root; roots_push(&rt->heap,&state_root,saved,3);
    char error[sizeof(rt->error)]; memcpy(error,rt->error,sizeof(error));
    const char *code=rt->error_code; size_t offset=rt->error_offset;
    bool raised=rt->raised, signaled=rt->signaled, restart=rt->restart_pending;
    bool exiting=rt->exit_pending, interrupted=rt->interrupted;
    int status=rt->exit_status;
    rt->error[0]=0; rt->error_code=NULL;
    rt->raised=rt->signaled=rt->restart_pending=rt->exit_pending=rt->interrupted=false;
    rt->condition_values[0]=rt->restart_transfer[0]=rt->restart_transfer[1]=V_NIL;
    size_t count=rt->open_port_count;
    Value *ports=count ? malloc(count*sizeof(Value)) : NULL;
    bool ok=!count || ports;
    if(!ok) rt_error(rt,"out of host memory");
    else {
        if(count) memcpy(ports,rt->open_ports,count*sizeof(Value));
        Roots roots; roots_push(&rt->heap,&roots,ports,count);
        for(size_t i=0;ok && i<count;i++) {
            if(!runtime_poll_interrupt(rt)) { ok=false; break; }
            if(!info(ports[i])->input && !info(ports[i])->closed)
                ok=flush(rt,&ports[i]);
        }
        roots_pop(&rt->heap,&roots);
    }
    free(ports);
    if(ok) {
        memcpy(rt->error,error,sizeof(error)); rt->error_code=code; rt->error_offset=offset;
        rt->raised=raised; rt->signaled=signaled; rt->restart_pending=restart;
        rt->exit_pending=exiting; rt->exit_status=status; rt->interrupted=interrupted;
        rt->condition_values[0]=saved[0]; rt->restart_transfer[0]=saved[1]; rt->restart_transfer[1]=saved[2];
    }
    roots_pop(&rt->heap,&state_root); return ok;
}
