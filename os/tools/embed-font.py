#!/usr/bin/env python3
"""
Embed a binary file as a C++ byte array header.
Usage: embed-font.py <input.ttf> <output.hpp> <variable_name>
"""

import sys
import os

def main():
    if len(sys.argv) != 4:
        print("Usage: embed-font.py <input.ttf> <output.hpp> <variable_name>")
        sys.exit(1)

    input_file = sys.argv[1]
    output_file = sys.argv[2]
    var_name = sys.argv[3]

    with open(input_file, 'rb') as f:
        data = f.read()

    guard = os.path.basename(output_file).upper().replace('.', '_').replace('-', '_')

    # Generate hex array in chunks of 16 bytes per line
    hex_lines = []
    for i in range(0, len(data), 16):
        chunk = data[i:i+16]
        hex_bytes = ', '.join(f'0x{b:02x}' for b in chunk)
        hex_lines.append(f'  {hex_bytes},')

    header = f"""// Auto-generated file - do not edit
// Generated from: {os.path.basename(input_file)}
// Size: {len(data)} bytes

#ifndef {guard}
#define {guard}

#include <stddef.h>

static const unsigned char {var_name}_data[] = {{
{chr(10).join(hex_lines)}
}};

static const size_t {var_name}_size = {len(data)};

#endif // {guard}
"""

    with open(output_file, 'w') as f:
        f.write(header)

    print(f"Generated {output_file} ({len(data)} bytes)")

if __name__ == '__main__':
    main()
