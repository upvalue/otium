#!/usr/bin/env python3
"""Copy the files manifest.json uses from upstream into vendor/.

Upstream is about 160 MB, mostly knucleotide's input archive, which is only
fasta output and is generated at run time instead. This fetches the given
commit sparsely, copies the reference sources, expected outputs and licenses
unmodified, and records the commit in manifest.json.
"""
import argparse
import json
import re
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile

HERE = Path(__file__).resolve().parent
LISP_INCLUDE = ['bundle.cl']


def wanted(manifest):
    files = ['LICENSE'] + [f'bench/include/lisp/{name}' for name in LISP_INCLUDE]
    for name, entry in manifest['benchmarks'].items():
        directory = f'bench/algorithm/{name}'
        files += [f'{directory}/{source}' for source in sorted(set(entry['references'].values()))]
        files += [f'{directory}/{output}' for _, output in entry['tests']]
        files.append(f'{directory}/LICENSE?')
    return files


def main():
    parser = argparse.ArgumentParser(description=__doc__,
                                     formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument('commit', nargs='?', help='upstream commit (default: the manifest commit)')
    args = parser.parse_args()
    manifest_path = HERE / 'manifest.json'
    manifest = json.loads(manifest_path.read_text())
    commit = args.commit or manifest['upstream_commit']
    with tempfile.TemporaryDirectory(prefix='otium-plb-') as temp:
        checkout = Path(temp) / 'upstream'
        def git(*arguments):
            subprocess.run(['git', '-C', str(checkout), *arguments], check=True)
        subprocess.run(['git', 'clone', '--quiet', '--filter=blob:none', '--no-checkout',
                        manifest['upstream'], str(checkout)], check=True)
        git('sparse-checkout', 'set', '--no-cone', *[path.rstrip('?') for path in wanted(manifest)])
        git('checkout', '--quiet', commit)
        commit = subprocess.run(['git', '-C', str(checkout), 'rev-parse', 'HEAD'], check=True,
                                capture_output=True, text=True).stdout.strip()
        vendor = HERE / 'vendor'
        shutil.rmtree(vendor, ignore_errors=True)
        missing = []
        for path in wanted(manifest):
            optional = path.endswith('?')
            path = path.rstrip('?')
            source = checkout / path
            if not source.is_file():
                if not optional:
                    missing.append(path)
                continue
            target = vendor / path.removeprefix('bench/')
            target.parent.mkdir(parents=True, exist_ok=True)
            shutil.copyfile(source, target)
    if missing:
        print('error: missing upstream files:', *missing, sep='\n  ', file=sys.stderr)
        return 1
    # Replace only the commit so the manifest keeps its layout.
    text = manifest_path.read_text()
    manifest_path.write_text(re.sub(r'("upstream_commit": ")[0-9a-f]+', rf'\g<1>{commit}', text))
    print(f'vendored {commit}')
    return 0


if __name__ == '__main__':
    sys.exit(main())
