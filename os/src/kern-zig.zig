const SbiRet = extern struct {
    err: c_long,
    value: c_long,
};

extern fn sbi_call(arg0: c_long, arg1: c_long, arg2: c_long, arg3: c_long, arg4: c_long, arg5: c_long, fid: c_long, eid: c_long) SbiRet;

extern fn otk_c_setup() void;
extern fn otk_exit() void;
extern fn malloc(size: usize) *anyopaque;
extern fn free(ptr: *anyopaque) void;

fn allocate(T: type, size: usize) []T {
    return @ptrCast(malloc(size));
}

export fn putchar(char: u8) void {
    _ = sbi_call(char, 0, 0, 0, 0, 0, 0, 1);
}

export fn kernel_main() void {
    otk_c_setup();

    putchar('H');
    putchar('e');
    putchar('l');
    putchar('l');
    putchar('o');
    putchar(',');
    putchar(' ');
    putchar('W');
    putchar('o');
    putchar('r');
    putchar('l');
    putchar('d');
    putchar('!');
    putchar('\n');

    // var exampleString = allocate(u8, 14);
    // exampleString[0] = 'H';
    // exampleString[1] = 'e';
    // exampleString[2] = 'l';
    // exampleString[3] = 'l';
    // exampleString[4] = 'o';
    // exampleString[5] = ',';
    // exampleString[6] = ' ';
    // exampleString[7] = 'W';
    // exampleString[8] = 'o';
    // exampleString[9] = 'r';
    // exampleString[10] = 'l';
    // exampleString[11] = 'd';
    // exampleString[12] = '!';
    // exampleString[13] = '\n';

    otk_exit();
}
