"""Decode SDL BMP captures for pixel assertions."""
from pathlib import Path
import struct


def bmp_pixels(path):
    """Return width, height and rows of RGB bytes, top row first."""
    data = Path(path).read_bytes()
    if data[:2] != b'BM':
        raise ValueError('Expected BMP capture')
    offset = struct.unpack_from('<I', data, 10)[0]
    width, height = struct.unpack_from('<ii', data, 18)
    bits, compression = struct.unpack_from('<HI', data, 28)
    if width <= 0 or height == 0 or bits not in (24, 32) or compression not in (0, 3):
        raise ValueError('Unsupported BMP capture format')
    stride = ((width * bits + 31) // 32) * 4
    rows = []
    masks = struct.unpack_from('<III', data, 54) if compression == 3 else (0xff0000, 0xff00, 0xff)
    for y in range(abs(height)):
        start = offset + (height - 1 - y if height > 0 else y) * stride
        row = bytearray()
        for x in range(width):
            pos = start + x * (bits // 8)
            value = int.from_bytes(data[pos:pos + bits // 8], 'little')
            row.extend((value & m) >> ((m & -m).bit_length() - 1) for m in masks)
        rows.append(bytes(row))
    return width, abs(height), rows


def pixel(path, x, y):
    """The RGB value at x, y in a BMP capture."""
    rows = bmp_pixels(path)[2]
    return tuple(rows[y][x * 3:x * 3 + 3])
