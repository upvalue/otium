#!/bin/bash
set -euo pipefail

# Configuration script for Otium OS
# Generates otconfig.h from otconfig.h.tmpl

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
TEMPLATE="$SCRIPT_DIR/otconfig.h.tmpl"
OUTPUT="$SCRIPT_DIR/otconfig.h"

# Default mode
MODE="KERNEL_PROG_SHELL"

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
    *)
      echo "Unknown option: $arg"
      echo "Usage: $0 [--default|--shell|--test-hello|--test-mem|--test-alternate]"
      exit 1
      ;;
  esac
done

# Generate otconfig.h from template
sed "s/__KERNEL_PROG_PLACEHOLDER__/$MODE/" "$TEMPLATE" > "$OUTPUT"

echo "Generated $OUTPUT with KERNEL_PROG=$MODE"
