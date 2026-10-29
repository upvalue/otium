#!/usr/bin/env python3
"""Run Programming-Language-Benchmarks problems on Otium and reference languages."""

import argparse
import csv
import hashlib
import json
import os
from pathlib import Path
import platform
import shutil
import subprocess
import sys
import tempfile

# Repository modules, for running this file directly.
sys.path[:0] = [str(Path(__file__).resolve().parents[2] / part)
                for part in ('', 'tools', 'benchmarks')]
from otium_tools import command, metadata, run_directory
import common

HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[1]
VENDOR = HERE / 'vendor'
HEAP_MAX = 1024 * 1024 * 1024
SIZES = ('test', 'small', 'full')

# Reference languages, in run order. Otium runs last so its output can be
# compared with a reference's when upstream has no expected output.
REFERENCES = {
    # Pinned runs tell Python 3.13+ it has one CPU, so multiprocessing
    # programs use one process instead of starting workers on one core.
    'python': {'run': ['python3', '-OO'], 'version': ['python3', '--version'],
               'pinned_env': {'PYTHON_CPU_COUNT': '1'}},
    'sbcl': {'run': None, 'version': ['sbcl', '--version']},
    'lua': {'run': ['lua'], 'version': ['lua', '-v']},
    'luajit': {'run': ['luajit'], 'version': ['luajit', '-v']},
}
LANGUAGES = [*REFERENCES, 'otium']

FIELDS = ['implementation', 'language', 'benchmark', 'variant', 'parity', 'source', 'size',
          'input', 'sample', 'wall_seconds', 'max_rss_kb', 'status', 'reason', 'checked_against',
          'threaded', 'output_sha256', 'platform', 'runtime_flags', 'upstream_commit',
          'otium_commit']


def parse_args():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--otium', type=Path, help='executable to run (default: the ./otium launcher)')
    parser.add_argument('--only', nargs='+', metavar='NAME', help='problems to run; default: all')
    parser.add_argument('--languages', nargs='+', choices=LANGUAGES, default=LANGUAGES)
    parser.add_argument('--otium-modes', nargs='+', choices=['native', 'vm'], default=['native'],
                        help='run Otium as native code, on the VM (--c.vm), or both (default: native)')
    parser.add_argument('--size', choices=SIZES, default='full',
                        help='test inputs are checked against upstream output (default: full)')
    parser.add_argument('--runs', type=int, default=3, help='samples per benchmark (default: 3)')
    parser.add_argument('--timeout', type=float, default=300.0, help='seconds per sample')
    parser.add_argument('--cpu', default='auto',
                        help="CPU to pin every process to with taskset, or 'none' (default: the last allowed CPU)")
    parser.add_argument('--output', type=Path, help='directory for CSV, status, and provenance')
    parser.add_argument('--build-label', default='unspecified')
    args = parser.parse_args()
    if args.runs < 1:
        parser.error('--runs must be positive')
    if args.timeout <= 0:
        parser.error('--timeout must be positive')
    return args


def git_value(*args):
    result = subprocess.run(['git', '-C', str(ROOT), *args], text=True, capture_output=True, check=False)
    return result.stdout.strip() if result.returncode == 0 else 'unknown'


def installed():
    """Version of each reference language, or None when it is missing."""
    return {name: common.version(spec['version']) for name, spec in REFERENCES.items()}


def shorten(reason):
    # Keep both ends: Otium puts the error first, Python tracebacks put it last.
    reason = ' '.join(reason.split())
    return reason if len(reason) <= 400 else reason[:150] + ' ... ' + reason[-245:]


def md5_body(output):
    """Replace a PBM body with its md5, as upstream mandelbrot programs print it."""
    header, _, rest = output.partition(b'\n')
    size, _, body = rest.partition(b'\n')
    return header + b'\n' + size + b'\n' + hashlib.md5(body).hexdigest().encode() + b'\n'


def normalize(output):
    return output.rstrip() + b'\n'


def fasta_inputs(entry, size, directory):
    """Generate knucleotide's <n>_in files with upstream's Python fasta."""
    names = [test[0] for test in entry['tests']] if size == 'test' else [entry['sizes'][size]]
    for name in names:
        path = directory / name
        if not path.exists():
            count = name.removesuffix('_in')
            with path.open('wb') as output:
                subprocess.run([sys.executable, '-I', str(VENDOR / 'algorithm/fasta/1.py'), count],
                               stdout=output, check=True)
    return names


def build_sbcl(source, directory, timeout):
    """Build an SBCL image the way upstream does; return its path or an error."""
    directory.mkdir(parents=True)
    shutil.copyfile(source, directory / 'app.cl')
    shutil.copyfile(VENDOR / 'include/lisp/bundle.cl', directory / 'bundle.cl')
    try:
        result = subprocess.run(['sbcl', '--dynamic-space-size', '8Gb', '--noinform', '--disable-debugger',
                                 '--non-interactive', '--load', 'bundle.cl'],
                                cwd=directory, capture_output=True, text=True, timeout=timeout)
    except subprocess.TimeoutExpired:
        return None, 'sbcl build timed out'
    if result.returncode != 0 or not (directory / 'app').is_file():
        return None, 'sbcl build failed: ' + (result.stderr or result.stdout)[-400:]
    return directory / 'app', ''


def main():
    args = parse_args()
    manifest = json.loads((HERE / 'manifest.json').read_text())
    selected = args.only or list(manifest['benchmarks'])
    unknown = [name for name in selected if name not in manifest['benchmarks']]
    if unknown:
        print(f'error: unknown benchmark: {unknown[0]}', file=sys.stderr)
        return 2
    otium = command(isolated=False, executable=args.otium)
    if 'otium' in args.languages and not Path(otium[0]).is_file():
        print(f'error: executable not found: {otium[0]}', file=sys.stderr)
        return 2
    cpu = common.choose_cpu(args.cpu)
    versions = installed()
    for name, found in versions.items():
        if name in args.languages:
            print(f'{name}: {found or "not installed"}')
    output_dir = args.output or run_directory('plb')
    output_dir.mkdir(parents=True, exist_ok=True)

    otium_commit = git_value('rev-parse', '--short', 'HEAD')
    dirty = bool(git_value('status', '--porcelain'))
    otium_name = f'{otium_commit}{"-dirty" if dirty else ""}'
    host = f'{platform.system()} {platform.release()} {platform.machine()}'
    upstream_commit = manifest['upstream_commit'][:12]
    rows, commands = [], []
    had_failure = False

    def row(language, implementation, name, entry, variant, source, size, input_text, sample,
            status, wall=None, rss=None, reason='', checked='', digest='', flags=''):
        port = entry.get('port', {})
        rows.append({
            'implementation': implementation, 'language': language, 'benchmark': name,
            'variant': variant, 'parity': port.get('parity', 'A') if variant == 'port' else '',
            'source': source, 'size': size, 'input': input_text, 'sample': sample,
            'wall_seconds': '' if wall is None else f'{wall:.9f}',
            'max_rss_kb': '' if rss is None else rss, 'status': status,
            'reason': shorten(reason), 'checked_against': checked,
            'threaded': 'yes' if language in entry.get('threaded', []) else '',
            'output_sha256': digest, 'platform': host, 'runtime_flags': flags,
            'upstream_commit': upstream_commit, 'otium_commit': otium_commit})

    with tempfile.TemporaryDirectory(prefix='otium-plb-') as temp_dir:
        temp = Path(temp_dir)
        inputs_dir = temp / 'inputs'
        inputs_dir.mkdir()
        for name in selected:
            entry = manifest['benchmarks'][name]
            algorithm = VENDOR / 'algorithm' / name
            work = temp / name
            work.mkdir()
            if entry.get('fasta_input'):
                for input_name in fasta_inputs(entry, args.size, inputs_dir):
                    (work / input_name).symlink_to(inputs_dir / input_name)
            if args.size == 'test':
                cases = [(text, (algorithm / output).read_bytes()) for text, output in entry['tests']]
            else:
                cases = [(entry['sizes'][args.size], None)]

            # Each runnable implementation: (language, implementation, variant, source, command).
            runnable = []
            for language in args.languages:
                if language == 'otium':
                    port = HERE / 'ports' / f'{name}.ot'
                    for mode in args.otium_modes:
                        implementation = f'otium-{mode}-{otium_name}'
                        if not port.is_file():
                            for text, _ in cases:
                                row('otium', implementation, name, entry, 'port', '', args.size, text, 0,
                                    'unsupported', reason='no port yet')
                            continue
                        flags = ['--no-project', f'--c.heap-max={HEAP_MAX}', '-L', str(HERE / 'lib'),
                                 *(['--c.vm'] if mode == 'vm' else [])]
                        runnable.append(('otium', implementation, 'port', port.name,
                                         [*otium, *flags, str(port), '--'], {}))
                    continue
                source = entry['references'].get(language)
                implementation = f'{language}-{versions[language]}' if versions[language] else language
                if source is None or not versions[language]:
                    status, reason = (('unsupported', 'no upstream program') if source is None
                                      else ('missing', f'{language} not installed'))
                    for text, _ in cases:
                        row(language, implementation, name, entry, 'upstream', source or '', args.size,
                            text, 0, status, reason=reason)
                    continue
                if language == 'sbcl':
                    image, error = build_sbcl(algorithm / source, temp / 'sbcl' / name, args.timeout)
                    if not image:
                        for text, _ in cases:
                            row(language, implementation, name, entry, 'upstream', source, args.size,
                                text, 0, 'crashed', reason=error)
                        had_failure = True
                        continue
                    run = [str(image)]
                else:
                    run = [*REFERENCES[language]['run'], str(algorithm / source)]
                env = REFERENCES[language].get('pinned_env', {}) if cpu is not None else {}
                runnable.append((language, implementation, 'upstream', source, run, env))

            for text, expected in cases:
                checked = 'upstream' if expected is not None else ''
                expected_digest = None
                for language, implementation, variant, source, run, env in runnable:
                    full = common.pinned([*run, *text.split()], cpu)
                    commands.append(full)
                    flags = ' '.join([*(f'{k}={v}' for k, v in env.items()),
                                      *(a for a in run if a.startswith('-'))])
                    for sample in range(1, args.runs + 1):
                        code, wall, stdout, stderr, rss = common.execute(
                            full, args.timeout, cwd=work, binary=True, env={**os.environ, **env})
                        digest, reason, against = '', '', checked
                        if code == 'timeout':
                            status, reason = 'timeout', f'timed out after {args.timeout:g}s'
                        elif code != 0:
                            status = 'crashed'
                            reason = stderr or stdout.decode(errors='replace') or f'exit status {code}'
                        else:
                            if variant == 'port' and entry.get('md5_body'):
                                stdout = md5_body(stdout)
                            output = normalize(stdout)
                            digest = hashlib.sha256(output).hexdigest()
                            if expected is not None:
                                ok = output == normalize(expected)
                            elif expected_digest is not None:
                                ok, against = digest == expected_digest[0], expected_digest[1]
                            else:
                                # The first successful run sets the answer for later ones.
                                ok = True
                                if variant == 'upstream':
                                    expected_digest = (digest, implementation)
                            status = 'ok' if ok else 'incorrect'
                        row(language, implementation, name, entry, variant, source, args.size, text,
                            sample, status, wall, rss, reason, against, digest, flags)
                        print(f'{name} {text} {implementation} sample {sample}: {status}')
                        had_failure |= status not in ('ok',)

    with (output_dir / 'results.csv').open('w', newline='') as output:
        writer = csv.DictWriter(output, fieldnames=FIELDS, lineterminator='\n')
        writer.writeheader()
        writer.writerows(rows)
    lines = ['# Benchmark status', '',
             '| benchmark | input | implementation | sample | status | wall | max RSS KB |',
             '|---|---|---|---:|---|---:|---:|']
    for r in rows:
        lines.append(f"| {r['benchmark']} | {r['input']} | {r['implementation']} | {r['sample']} | "
                     f"{r['status']} | {r['wall_seconds']} | {r['max_rss_kb']} |")
    (output_dir / 'STATUS.md').write_text('\n'.join(lines) + '\n')
    provenance = metadata(commands, build_label=args.build_label)
    provenance.update({'suite': 'plb', 'size': args.size, 'runs': args.runs, 'cpu': cpu,
                       'versions': {name: versions.get(name) for name in args.languages if name != 'otium'},
                       'upstream_commit': manifest['upstream_commit']})
    (output_dir / 'metadata.json').write_text(json.dumps(provenance, indent=2) + '\n')
    print(f'Results: {output_dir}')
    return 1 if had_failure else 0


if __name__ == '__main__':
    sys.exit(main())
