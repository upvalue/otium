#!/usr/bin/env bash
# Extract FAT32 disk image contents to fs-out/ directory
#
# Usage: extract-disk-image.sh [input-image]
# Default input: disk.fat.img in project root

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"

INPUT_IMAGE="${1:-$PROJECT_ROOT/disk.fat.img}"
FS_OUT_DIR="$PROJECT_ROOT/fs-out"

# Check if disk image exists
if [ ! -f "$INPUT_IMAGE" ]; then
    echo "Note: $INPUT_IMAGE does not exist, skipping extraction"
    exit 0
fi

echo "Extracting disk image to $FS_OUT_DIR"

# Create/clear output directory
mkdir -p "$FS_OUT_DIR"
rm -rf "$FS_OUT_DIR"/*

# Extract all files from root of disk image
# mcopy with wildcard extracts all files
mcopy -i "$INPUT_IMAGE" -s ::/ "$FS_OUT_DIR/" 2>/dev/null || true

echo "Extraction complete. Contents:"
ls -la "$FS_OUT_DIR"
