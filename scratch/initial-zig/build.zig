const std = @import("std");

pub fn build(b: *std.Build) void {
    const target = b.resolveTargetQuery(.{
        .cpu_arch = .riscv32,
        .os_tag = .freestanding,
        .abi = .none,
    });

    const optimize = b.standardOptimizeOption(.{});

    // Create a root module with our Zig code
    const root_module = b.createModule(.{
        .root_source_file = b.path("src/kernel.zig"),
        .target = target,
        .optimize = optimize,
    });

    // Create the kernel executable
    const kernel = b.addExecutable(.{
        .name = "kernel.elf",
        .root_module = root_module,
    });

    kernel.linkage = .static;

    // Configure for bare-metal embedded target
    kernel.setLinkerScript(b.path("src/kernel.ld"));
    kernel.entry = .{ .symbol_name = "boot" };

    // Add C source files
    kernel.addCSourceFile(.{
        .file = b.path("src/kernel.c"),
        .flags = &.{
            "-std=c11",
            "-ffreestanding",
            "-nostdlib",
            "-fno-stack-protector",
            "-Wall",
            "-Wextra",
        },
    });

    // Add assembly source file
    kernel.addAssemblyFile(b.path("src/boot.s"));

    // Install the kernel
    b.installArtifact(kernel);
}
