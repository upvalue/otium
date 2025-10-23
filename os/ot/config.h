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

// Selected kernel program (modified by config.sh)
#define KERNEL_PROG KERNEL_PROG_SHELL

// Log levels
#define LSILENT 0
#define LSOFT 1
#define LLOUD 2

// Subsystem log levels (configured by config.sh)
#define LOG_GENERAL LSOFT
#define LOG_MEM LSOFT
#define LOG_PROC LSOFT
#define LOG_IPC LSOFT

#endif
