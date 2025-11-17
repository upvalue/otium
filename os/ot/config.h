#ifndef OT_CONFIG_H
#define OT_CONFIG_H

// Available kernel program modes

// Runs an interactive Tcl shell
#define KERNEL_PROG_SHELL 0

// Runs a program that prints hello world and exits
#define KERNEL_PROG_TEST_HELLO 1

// Runs a program that exercises the memory subsystem
#define KERNEL_PROG_TEST_MEM 2

// Runs a program that alternates between programs and prints
// some text in order, proving that the scheduler work
#define KERNEL_PROG_TEST_ALTERNATE 3

#define KERNEL_PROG_TEST_USERSPACE 4

// Runs IPC test with fibonacci service
#define KERNEL_PROG_TEST_IPC 5

// Runs IPC ordering test to verify scheduling behavior
#define KERNEL_PROG_TEST_IPC_ORDERING 6

// Runs IPC test with generated fibonacci client/server code
#define KERNEL_PROG_TEST_IPC_CODEGEN 7

// Selected kernel program (modified by config.sh)
#define KERNEL_PROG KERNEL_PROG_TEST_IPC_CODEGEN

// Log levels
#define LSILENT 0
#define LSOFT 1
#define LLOUD 2

// Subsystem log levels (configured by config.sh)
#define LOG_GENERAL LSOFT
#define LOG_MEM LSOFT
#define LOG_PROC LSOFT
#define LOG_IPC LSOFT

// Graphics backend feature flags
#define OT_FEAT_GFX_UNSUPPORTED 0
#define OT_FEAT_GFX_VIRTIO 1

// Default to unsupported (can be overridden by build system)
#ifndef OT_FEAT_GFX
#define OT_FEAT_GFX OT_FEAT_GFX_UNSUPPORTED
#endif

#endif
