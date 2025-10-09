const k = @cImport({
    @cInclude("./kernel.h");
});

pub fn putchar(char: u8) void {
    k.sbi_call(char, 0, 0, 0, 0, 0, 0, 1);
}
