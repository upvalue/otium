// common.zig - C support function bindings

pub extern fn printf(fmt: [*:0]const u8, ...) void;
pub extern fn malloc(size: usize) *anyopaque;
pub extern fn free(ptr: *anyopaque) void;
