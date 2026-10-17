"""Embed Metal source and compiled SPIR-V for the rectangle pipeline."""
import json
from pathlib import Path
import subprocess
import sys
source, output, compiler = Path(sys.argv[1]), Path(sys.argv[2]), sys.argv[3]
lines = ['static const char rect_msl[] = ' + json.dumps((source/'rect.metal').read_text()) + ';']
for stage in ('vert', 'frag'):
    binary = output.with_suffix('.' + stage + '.spv')
    subprocess.run([compiler, str(source/('rect.'+stage)), '-o', str(binary)], check=True)
    lines.append('static const unsigned char rect_' + stage + '[] = {' +
                 ','.join(str(b) for b in binary.read_bytes()) + '};')
output.write_text('\n'.join(lines)+'\n')
