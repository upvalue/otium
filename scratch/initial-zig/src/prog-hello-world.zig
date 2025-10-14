const c = @import("common");

pub fn main() void {
    while (true) {
        const ch = c.getchar();
        if (ch == -1) {
            continue;
        }
        c.printf("You typed: %c\n", ch);
    }
    c.printf("Hello, world!\n");
}
