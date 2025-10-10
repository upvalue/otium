const SbiRet = extern struct {
    err: c_long,
    value: c_long,
};

extern fn sbi_call(arg0: c_long, arg1: c_long, arg2: c_long, arg3: c_long, arg4: c_long, arg5: c_long, fid: c_long, eid: c_long) SbiRet;

extern fn otk_c_setup() void;
extern fn otk_exit() void;
extern fn malloc(size: usize) *anyopaque;
extern fn free(ptr: *anyopaque) void;
extern fn printf(fmt: [*:0]const u8, ...) void;

fn allocate(T: type, size: usize) [*]T {
    const ptr = malloc(size * @sizeOf(T));
    return @ptrCast(ptr);
}

export fn putchar(char: u8) void {
    _ = sbi_call(char, 0, 0, 0, 0, 0, 0, 1);
}

export fn kernel_main() void {
    otk_c_setup();

    otk_exit();
}
