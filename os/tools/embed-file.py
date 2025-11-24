#!/usr/bin/env python3
"""
Embed a text file as a C++ string constant in a header file.
Usage: embed-file.py <input-file> <output-header> <variable-name>
"""

import sys
import os

def escape_c_string(s):
    """Escape a string for use in C++ string literal."""
    # Replace backslash first
    s = s.replace('\\', '\\\\')
    # Replace double quotes
    s = s.replace('"', '\\"')
    # Replace newlines
    s = s.replace('\n', '\\n')
    return s

def main():
    if len(sys.argv) != 4:
        print("Usage: embed-file.py <input-file> <output-header> <variable-name>")
        sys.exit(1)

    input_file = sys.argv[1]
    output_file = sys.argv[2]
    var_name = sys.argv[3]

    # Read input file
    with open(input_file, 'r') as f:
        content = f.read()

    # Escape for C++ string
    escaped = escape_c_string(content)

    # Generate header guard from output filename
    guard = os.path.basename(output_file).upper().replace('.', '_').replace('-', '_')

    # Generate header content
    header = f"""// Auto-generated file - do not edit
// Generated from: {input_file}

#ifndef {guard}
#define {guard}

constexpr const char *{var_name} = "{escaped}";

#endif // {guard}
"""

    # Write output file
    with open(output_file, 'w') as f:
        f.write(header)

    print(f"Generated {output_file} from {input_file}")

if __name__ == '__main__':
    main()
