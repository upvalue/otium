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

# Default graphics backend
GRAPHICS_BACKEND="OT_GRAPHICS_BACKEND_TEST"

# Default filesystem backend
FILESYSTEM_BACKEND="OT_FILESYSTEM_BACKEND_MEMORY"

# Default shell enable
SHELL_ENABLE="#define ENABLE_SHELL"

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
    --test-ipc)
      MODE="KERNEL_PROG_TEST_IPC"
      ;;
    --test-ipc-ordering)
      MODE="KERNEL_PROG_TEST_IPC_ORDERING"
      ;;
    --test-ipc-codegen)
      MODE="KERNEL_PROG_TEST_IPC_CODEGEN"
      ;;
    --test-graphics)
      MODE="KERNEL_PROG_TEST_GRAPHICS"
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
    --graphics-backend=*|--graphics=*)
      backend="${arg#*=}"
      case $backend in
        none) GRAPHICS_BACKEND="OT_GRAPHICS_BACKEND_NONE" ;;
        test) GRAPHICS_BACKEND="OT_GRAPHICS_BACKEND_TEST" ;;
        virtio) GRAPHICS_BACKEND="OT_GRAPHICS_BACKEND_VIRTIO" ;;
        wasm) GRAPHICS_BACKEND="OT_GRAPHICS_BACKEND_WASM" ;;
        *) echo "Invalid graphics backend: $backend (use none|test|virtio|wasm)"; exit 1 ;;
      esac
      ;;
    --filesystem-backend=*|--filesystem=*)
      backend="${arg#*=}"
      case $backend in
        none) FILESYSTEM_BACKEND="OT_FILESYSTEM_BACKEND_NONE" ;;
        memory) FILESYSTEM_BACKEND="OT_FILESYSTEM_BACKEND_MEMORY" ;;
        *) echo "Invalid filesystem backend: $backend (use none|memory)"; exit 1 ;;
      esac
      ;;
    --shell=*)
      value="${arg#*=}"
      case $value in
        true) SHELL_ENABLE="#define ENABLE_SHELL" ;;
        false) SHELL_ENABLE="// #define ENABLE_SHELL" ;;
        *) echo "Invalid shell value: $value (use true|false)"; exit 1 ;;
      esac
      ;;
    *)
      echo "Unknown option: $arg"
      echo "Usage: $0 [--default|--shell|--test-hello|--test-mem|--test-alternate|--test-ipc|--test-ipc-ordering|--test-ipc-codegen|--test-graphics]"
      echo "          [--log-general=silent|soft|loud]"
      echo "          [--log-mem=silent|soft|loud]"
      echo "          [--log-proc=silent|soft|loud]"
      echo "          [--log-ipc=silent|soft|loud]"
      echo "          [--graphics-backend=none|test|virtio|wasm]"
      echo "          [--filesystem-backend=none|memory]"
      echo "          [--shell=true|false]"
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
    -e "s/__GRAPHICS_BACKEND__/$GRAPHICS_BACKEND/" \
    -e "s/__FILESYSTEM_BACKEND__/$FILESYSTEM_BACKEND/" \
    -e "s|__SHELL_ENABLE__|$SHELL_ENABLE|" \
    "$TEMPLATE" > "$OUTPUT"

# Create build directory if it doesn't exist
mkdir -p "$SCRIPT_DIR/build"

# Generate runtime config files for scripts
RUNTIME_CONFIG_SH="$SCRIPT_DIR/build/runtime-config.sh"
RUNTIME_CONFIG_JSON="$SCRIPT_DIR/build/runtime-config.json"

# Shell script version
cat > "$RUNTIME_CONFIG_SH" << EOF
# Generated runtime configuration (sourced by run scripts)
RUNTIME_GRAPHICS_BACKEND="$GRAPHICS_BACKEND"
RUNTIME_FILESYSTEM_BACKEND="$FILESYSTEM_BACKEND"
RUNTIME_SHELL_ENABLED=$([ "$SHELL_ENABLE" = "#define ENABLE_SHELL" ] && echo "true" || echo "false")
EOF

# JSON version
cat > "$RUNTIME_CONFIG_JSON" << EOF
{
  "graphics": {
    "backend": "$GRAPHICS_BACKEND",
    "enabled": $([ "$GRAPHICS_BACKEND" = "OT_GRAPHICS_BACKEND_NONE" ] && echo "false" || echo "true")
  },
  "filesystem": {
    "backend": "$FILESYSTEM_BACKEND",
    "enabled": $([ "$FILESYSTEM_BACKEND" = "OT_FILESYSTEM_BACKEND_NONE" ] && echo "false" || echo "true")
  },
  "shell": {
    "enabled": $([ "$SHELL_ENABLE" = "#define ENABLE_SHELL" ] && echo "true" || echo "false")
  }
}
EOF

echo "Generated $OUTPUT with KERNEL_PROG=$MODE, LOG_GENERAL=$LOG_GENERAL, LOG_MEM=$LOG_MEM, LOG_PROC=$LOG_PROC, LOG_IPC=$LOG_IPC, GRAPHICS_BACKEND=$GRAPHICS_BACKEND, FILESYSTEM_BACKEND=$FILESYSTEM_BACKEND, SHELL=$SHELL_ENABLE"
echo "Generated $RUNTIME_CONFIG_SH and $RUNTIME_CONFIG_JSON for run scripts"
