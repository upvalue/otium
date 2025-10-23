#!/bin/bash
set -euo pipefail

# Configuration script for Otium OS
# Generates otconfig.h from otconfig.h.tmpl

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
TEMPLATE="$SCRIPT_DIR/ot/config.h.tmpl"
OUTPUT="$SCRIPT_DIR/ot/config.h"

# Default mode
MODE="KERNEL_PROG_SHELL"

# Default log levels (LSILENT=0, LSOFT=1, LLOUD=2)
LOG_GENERAL="LSOFT"
LOG_MEM="LSOFT"
LOG_PROC="LSOFT"
LOG_IPC="LSOFT"

# Parse arguments
for arg in "$@"; do
  case $arg in
    --default|--shell)
      MODE="KERNEL_PROG_SHELL"
      ;;
    --test-hello)
      MODE="KERNEL_PROG_TEST_HELLO"
      ;;
    --test-mem)
      MODE="KERNEL_PROG_TEST_MEM"
      ;;
    --test-alternate)
      MODE="KERNEL_PROG_TEST_ALTERNATE"
      ;;
    --log-general=*)
      level="${arg#*=}"
      case $level in
        silent) LOG_GENERAL="LSILENT" ;;
        soft) LOG_GENERAL="LSOFT" ;;
        loud) LOG_GENERAL="LLOUD" ;;
        *) echo "Invalid log level: $level (use silent|soft|loud)"; exit 1 ;;
      esac
      ;;
    --log-mem=*)
      level="${arg#*=}"
      case $level in
        silent) LOG_MEM="LSILENT" ;;
        soft) LOG_MEM="LSOFT" ;;
        loud) LOG_MEM="LLOUD" ;;
        *) echo "Invalid log level: $level (use silent|soft|loud)"; exit 1 ;;
      esac
      ;;
    --log-proc=*)
      level="${arg#*=}"
      case $level in
        silent) LOG_PROC="LSILENT" ;;
        soft) LOG_PROC="LSOFT" ;;
        loud) LOG_PROC="LLOUD" ;;
        *) echo "Invalid log level: $level (use silent|soft|loud)"; exit 1 ;;
      esac
      ;;
    --log-ipc=*)
      level="${arg#*=}"
      case $level in
        silent) LOG_IPC="LSILENT" ;;
        soft) LOG_IPC="LSOFT" ;;
        loud) LOG_IPC="LLOUD" ;;
        *) echo "Invalid log level: $level (use silent|soft|loud)"; exit 1 ;;
      esac
      ;;
    *)
      echo "Unknown option: $arg"
      echo "Usage: $0 [--default|--shell|--test-hello|--test-mem|--test-alternate]"
      echo "          [--log-general=silent|soft|loud]"
      echo "          [--log-mem=silent|soft|loud]"
      echo "          [--log-proc=silent|soft|loud]"
      echo "          [--log-ipc=silent|soft|loud]"
      exit 1
      ;;
  esac
done

# Generate otconfig.h from template
sed -e "s/__KERNEL_PROG_PLACEHOLDER__/$MODE/" \
    -e "s/__LOG_GENERAL__/$LOG_GENERAL/" \
    -e "s/__LOG_MEM__/$LOG_MEM/" \
    -e "s/__LOG_PROC__/$LOG_PROC/" \
    -e "s/__LOG_IPC__/$LOG_IPC/" \
    "$TEMPLATE" > "$OUTPUT"

echo "Generated $OUTPUT with KERNEL_PROG=$MODE, LOG_GENERAL=$LOG_GENERAL, LOG_MEM=$LOG_MEM, LOG_PROC=$LOG_PROC, LOG_IPC=$LOG_IPC"
