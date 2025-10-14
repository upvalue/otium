const c = @import("common");

var buffer: [1024]u8 = undefined;

pub fn main() void {
    var i: usize = 0;
    c.printf("Ready for input\n");
    while (true) {
        c.memset(&buffer, 0, 1024);
        i = 0;
        c.printf("> ");
        input: while (true) {
            const ch = c.getchar();
            if (ch == -1) {
                continue :input;
            }

            c.printf("%c", ch);

            buffer[i] = @intCast(ch);
            i += 1;
            if (i == 1023 or (ch == 13)) {
                break;
            }
        }

        buffer[i] = 0;
        if (buffer[0] == 0) {
            break;
        }
        c.printf("\n%s\n", &buffer);
    }
}
