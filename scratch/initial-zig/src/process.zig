const C = @import("constants");
const c = @import("common");

pub const ProcessState = enum {
    running,
    unused,
};

// Process context
pub const ProcessCtx = struct {
    //
    return_addr: usize,
    stack_ptr: usize,

    s0: usize,
    s1: usize,
    s2: usize,
    s3: usize,
    s4: usize,
    s5: usize,
    s6: usize,
    s7: usize,
    s8: usize,
    s9: usize,
    s10: usize,
    s11: usize,
};

// Core process structure
pub const Process = struct { pid: u32, context: ProcessCtx };

pub var procs: [C.MAX_PROCS]Process = {};

pub fn process_create() *Process {
    var free_proc: *Process = undefined;
    for (procs, 0..) |proc, i| {
        if (proc.state == .unused) {
            proc.state = .running;
            proc.pid = i + 1;
            free_proc = &procs[i];
            break;
        }
    }

    if (!free_proc) {
        c.panic("reached process limit");
    }

    return free_proc;
}
