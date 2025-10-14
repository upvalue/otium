const std = @import("std");

const Program = enum {
    hello_world,
    echo,
};

const TargetArch = enum {
    riscv32,
    wasm32,
};

pub fn build(b: *std.Build) void {
    // Options setup
    // User can pick target architecture and main program to run
    const targetArch = b.option(TargetArch, "target-arch", "The architecture to build for") orelse .riscv32;

    const target = b.resolveTargetQuery(.{
        .cpu_arch = switch (targetArch) {
            .riscv32 => .riscv32,
            .wasm32 => .wasm32,
        },
        .os_tag = .freestanding,
        .abi = .none,
    });

    const prog = b.option(Program, "program", "The program to build") orelse .hello_world;
    const options = b.addOptions();

    options.addOption(TargetArch, "target-arch", targetArch);
    options.addOption(Program, "program", prog);

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
    kernel.root_module.addOptions("config", options);

    kernel.linkage = .static;

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
        .file = b.path("src/common.c"),
        .flags = cflags_strict,
    });

    kernel.addCSourceFile(.{
        .file = b.path("src/vendor/tlsf.c"),
        .flags = cflags,
    });

    kernel.entry = .{ .symbol_name = "kernel_enter" };

    // Architecture specific support code
    if (targetArch == .riscv32) {
        kernel.setLinkerScript(b.path("src/riscv32/link.ld"));
        kernel.addCSourceFile(.{
            .file = b.path("src/riscv32/kernel.c"),
            .flags = cflags_strict,
        });

        // Add assembly source file
        kernel.addAssemblyFile(b.path("src/riscv32/support.s"));
    } else if (targetArch == .wasm32) {
        kernel.addCSourceFile(.{
            .file = b.path("src/wasm32/kernel.c"),
            .flags = cflags_strict,
        });
        kernel.root_module.addCMacro("__wasm__", "1");
        kernel.import_symbols = true;
    }

    // Install the kernel
    b.installArtifact(kernel);
}
