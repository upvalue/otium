const SbiRet = extern struct {
    err: c_long,
    value: c_long,
};

extern fn sbi_call(arg0: c_long, arg1: c_long, arg2: c_long, arg3: c_long, arg4: c_long, arg5: c_long, fid: c_long, eid: c_long) SbiRet;

export fn putchar(char: u8) void {
    _ = sbi_call(char, 0, 0, 0, 0, 0, 0, 1);
}
