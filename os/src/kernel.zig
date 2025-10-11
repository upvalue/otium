const c = @import("common");
const config = @import("config");

const helloWorld = @import("prog-hello-world");
const echo = @import("prog-echo");

const SbiRet = extern struct {
    err: c_long,
    value: c_long,
};

extern fn sbi_call(arg0: c_long, arg1: c_long, arg2: c_long, arg3: c_long, arg4: c_long, arg5: c_long, fid: c_long, eid: c_long) SbiRet;

extern fn otk_c_setup() void;
extern fn otk_exit() void;

fn allocate(T: type, size: usize) [*]T {
    const ptr = c.malloc(size * @sizeOf(T));
    return @ptrCast(ptr);
}

export fn kernel_main() void {
    otk_c_setup();

    switch (config.program) {
        .hello_world => {
            helloWorld.main();
        },
        .echo => {
            echo.main();
        },
    }

    otk_exit();
}
