const std = @import("std");

const Program = enum {
    hello_world,
    echo,
};

pub fn build(b: *std.Build) void {
    const target = b.resolveTargetQuery(.{
        .cpu_arch = .riscv32,
        .os_tag = .freestanding,
        .abi = .none,
    });

    const optimize = b.standardOptimizeOption(.{});

    // Create shared common module (C bindings)
    const common_module = b.createModule(.{
        .root_source_file = b.path("src/common.zig"),
        .target = target,
        .optimize = optimize,
    });

    // Create program modules
    const prog_hello_world = b.createModule(.{
        .root_source_file = b.path("src/prog-hello-world.zig"),
        .target = target,
        .optimize = optimize,
    });
    prog_hello_world.addImport("common", common_module);

    const prog_echo = b.createModule(.{
        .root_source_file = b.path("src/prog-echo.zig"),
        .target = target,
        .optimize = optimize,
    });
    prog_echo.addImport("common", common_module);

    // Create a root module with our Zig code
    const root_module = b.createModule(.{
        .root_source_file = b.path("src/kernel.zig"),
        .target = target,
        .optimize = optimize,
    });

    root_module.addImport("common", common_module);
    root_module.addImport("prog-hello-world", prog_hello_world);
    root_module.addImport("prog-echo", prog_echo);

    // Create the kernel executable
    const kernel = b.addExecutable(.{
        .name = "kernel.elf",
        .root_module = root_module,
    });

    // Program selection: allows determining what the main program to run is
    const prog = b.option(Program, "program", "The program to build") orelse .hello_world;
    const options = b.addOptions();
    options.addOption(Program, "program", prog);
    kernel.root_module.addOptions("config", options);

    kernel.linkage = .static;

    // Configure for bare-metal embedded target
    kernel.setLinkerScript(b.path("src/kernel.ld"));
    kernel.entry = .{ .symbol_name = "boot" };

    const cflags: []const []const u8 = &.{
        "-std=c11",
        "-ffreestanding",
        "-nostdlib",
        "-fno-stack-protector",
        "-Isrc/vendor",
    };

    const cflags_strict = cflags ++ .{
        "-Wall",
        "-Wextra",
    };

    // Add C source files
    kernel.addCSourceFile(.{
        .file = b.path("src/riscv32/kernel.c"),
        .flags = cflags_strict,
    });

    kernel.addCSourceFile(.{
        .file = b.path("src/common.c"),
        .flags = cflags_strict,
    });

    kernel.addCSourceFile(.{
        .file = b.path("src/vendor/tlsf.c"),
        .flags = cflags,
    });

    // Add assembly source file
    kernel.addAssemblyFile(b.path("src/riscv32/boot.s"));

    // Install the kernel
    b.installArtifact(kernel);
}
