#!/usr/bin/env bash
# Create FAT32 disk image from fs-in/ directory contents
#
# Usage: create-disk-image.sh [output-image]
# Default output: disk.fat.img in project root

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"

OUTPUT_IMAGE="${1:-$PROJECT_ROOT/disk.fat.img}"
FS_IN_DIR="$PROJECT_ROOT/fs-in"
IMAGE_SIZE_MB=128
VOLUME_LABEL="OTIUMFS"

# Check if fs-in exists and has files
if [ ! -d "$FS_IN_DIR" ]; then
    echo "Note: $FS_IN_DIR does not exist, skipping disk image creation"
    exit 0
fi

if [ -z "$(ls -A "$FS_IN_DIR" 2>/dev/null)" ]; then
    echo "Note: $FS_IN_DIR is empty, skipping disk image creation"
    exit 0
fi

echo "Creating ${IMAGE_SIZE_MB}MB FAT32 disk image: $OUTPUT_IMAGE"

# Create empty image file
dd if=/dev/zero of="$OUTPUT_IMAGE" bs=1M count=$IMAGE_SIZE_MB status=none

# Format as FAT32
mformat -F -v "$VOLUME_LABEL" -i "$OUTPUT_IMAGE" ::

# Copy all files from fs-in/ to the image
echo "Copying files from $FS_IN_DIR:"
for file in "$FS_IN_DIR"/*; do
    if [ -f "$file" ]; then
        filename=$(basename "$file")
        echo "  $filename"
        mcopy -i "$OUTPUT_IMAGE" "$file" ::
    fi
done

echo "Disk image created successfully"
