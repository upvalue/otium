#!/usr/bin/env python3
"""Compare two optimizer configurations of the same build.

A configuration is an OTIUM_OPT value (all, none, or components switched
off such as -direct,-transform:list) plus the boot file compiled with it:
otium-noopt.boot for none, otium.boot otherwise. Using one binary keeps
code layout changes in the runtime out of the comparison.

Runs the target benchmarks (R7RS and PLB ports), the boot build time (the
native compiler compiling the standard boot file), the time to load some
larger modules from source, and code size under each,
then prints per-benchmark median ratios (b / a) and the geometric mean.
Pass --a all --b all for an A/A run, which measures noise. See
scratch/optimizer-design.md, section 8.
"""
import argparse
import csv
import math
import os
import statistics
import subprocess
import sys
import tempfile
import time
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
BUILD = Path(os.environ.get('OTIUM_BUILD') or
             ROOT / 'build' / f'{os.uname().machine}-{sys.platform.rstrip("0123456789")}' / 'default')

R7RS = ['browse', 'conform', 'cpstak', 'deriv', 'destruc', 'diviter', 'divrec', 'earley', 'graphs',
        'lattice', 'mazefun', 'nboyer', 'sboyer', 'nqueens', 'paraffins', 'peval', 'primes',
        'quicksort', 'string', 'takl', 'ntakl', 'ack', 'fib', 'tak']
PLB = ['binarytrees', 'fannkuch-redux', 'knucleotide', 'lru', 'merkletrees', 'nsieve']
# Reported, but only watched for regressions.
NUMERIC_R7RS = ['fft', 'fibfp', 'mbrot', 'nbody', 'nucleic', 'pnpoly', 'ray', 'simplex', 'sumfp']
NUMERIC_PLB = ['mandelbrot', 'spectral-norm']


def environment(config):
    env = dict(os.environ)
    env['OTIUM_OPT'] = config
    env['OTIUM_BOOT'] = str(BUILD / ('otium-noopt.boot' if config == 'none' else 'otium.boot'))
    return env


def load(directory):
    """Benchmark name to the samples' seconds, from a runner's results.csv."""
    samples = {}
    with open(directory / 'results.csv') as f:
        for row in csv.DictReader(f):
            if row.get('status', 'ok') != 'ok' or not row.get('implementation', 'otium').startswith('otium'):
                continue
            seconds = row.get('reported_seconds') or row.get('wall_seconds')
            if seconds:
                samples.setdefault(row['benchmark'].split(':')[0], []).append(float(seconds))
    return samples


def run_suites(config, args, out):
    env = environment(config)
    results = {}
    r7rs = [b for b in args.only or R7RS + (NUMERIC_R7RS if args.numeric else []) if b not in PLB + NUMERIC_PLB]
    plb = [b for b in args.only or PLB + (NUMERIC_PLB if args.numeric else []) if b in PLB + NUMERIC_PLB]
    if r7rs:
        d = out / 'r7rs'
        subprocess.run([sys.executable, ROOT / 'benchmarks/r7rs/run.py', '--only', *r7rs, '--runs',
                        str(args.runs), '--cpu', 'auto', '--output', d], env=env, check=True,
                       stdout=subprocess.DEVNULL)
        results.update(load(d))
    if plb:
        d = out / 'plb'
        subprocess.run([sys.executable, ROOT / 'benchmarks/plb/run.py', '--only', *plb, '--languages', 'otium',
                        '--runs', str(args.runs), '--size', args.plb_size, '--cpu', 'auto', '--output', d],
                       env=env, check=True, stdout=subprocess.DEVNULL)
        results.update(load(d))
    return results


def boot_build(config, runs, out):
    """Seconds for the native compiler to compile the standard boot file."""
    env = environment(config)
    lib = ROOT / 'lib'
    primitives = ROOT / 'c/src/primitives.ot'
    times = []
    for i in range(runs):
        start = time.perf_counter()
        subprocess.run(['taskset', '-c', str(max(os.sched_getaffinity(0))), BUILD / 'otium', '--no-project',
                        '-L', lib, '-e',
                        f'(require otium.compiler.boot :as boot) '
                        f'(boot/build-standard "{out / "stage2.boot"}" "{lib}" "{primitives}") (exit 0)'],
                       env=env, check=True)
        times.append(time.perf_counter() - start)
    return times


# Larger modules loaded from source, compiled as they load. Manse's sources
# need its native libraries, so aren't here.
SOURCE_MODULES = ['otium.lint', 'otium.ui', 'otium.format']


def source_load(config, runs):
    """Seconds to start and load SOURCE_MODULES from source."""
    env = environment(config)
    requires = ' '.join(f'(require {m})' for m in SOURCE_MODULES)
    times = []
    for i in range(runs):
        start = time.perf_counter()
        subprocess.run(['taskset', '-c', str(max(os.sched_getaffinity(0))), BUILD / 'otium', '--no-project',
                        '-L', ROOT / 'lib', '-e', f'{requires} (exit 0)'], env=env, check=True)
        times.append(time.perf_counter() - start)
    return times


def summary(name, a, b):
    ma, mb = statistics.median(a), statistics.median(b)
    spread = max((max(s) - min(s)) / statistics.median(s) for s in (a, b))
    print(f'{name:16} {ma:9.3f} {mb:9.3f} {mb / ma:7.3f}   spread {spread:5.1%}')
    return mb / ma


def main():
    p = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    p.add_argument('--a', default='none', help='OTIUM_OPT for the baseline (default: none)')
    p.add_argument('--b', default='all', help='OTIUM_OPT for the comparison (default: all)')
    p.add_argument('--only', nargs='+', help='benchmarks to run; default: the target set')
    p.add_argument('--numeric', action='store_true', help='also run the numeric ports')
    p.add_argument('--runs', type=int, default=5)
    p.add_argument('--plb-size', default='full', choices=['test', 'small', 'full'])
    p.add_argument('--no-suites', action='store_true', help='only the boot build and code size')
    p.add_argument('--output', type=Path, help='directory to keep the runners\' results in')
    args = p.parse_args()

    out = args.output or Path(tempfile.mkdtemp(prefix='opt-compare-'))
    configs = {'a': args.a, 'b': args.b}
    suites, boots, loads = {}, {}, {}
    for key, config in configs.items():
        d = out / key
        d.mkdir(parents=True, exist_ok=True)
        print(f'{key}: OTIUM_OPT={config}', file=sys.stderr)
        boots[key] = boot_build(config, args.runs, d)
        loads[key] = source_load(config, args.runs)
        suites[key] = {} if args.no_suites else run_suites(config, args, d)

    print(f'{"":16} {"a":>9} {"b":>9} {"b/a":>7}')
    ratios = []
    for name in suites['a']:
        if name in suites['b']:
            ratio = summary(name, suites['a'][name], suites['b'][name])
            if name in R7RS + PLB:
                ratios.append(ratio)
    if ratios:
        print(f'{"target geomean":16} {"":9} {"":9} {math.exp(sum(map(math.log, ratios)) / len(ratios)):7.3f}')
    summary('boot build', boots['a'], boots['b'])
    summary('source load', loads['a'], loads['b'])
    for key, config in configs.items():
        boot = environment(config)['OTIUM_BOOT']
        print(f'boot file {key}: {os.path.getsize(boot)} bytes ({Path(boot).name})')
    print(f'results in {out}', file=sys.stderr)


if __name__ == '__main__':
    main()
