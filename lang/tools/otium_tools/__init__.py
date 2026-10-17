"""Repository execution helpers. Runtime behavior belongs in the launchers."""
import argparse
import datetime
import json
import os
from pathlib import Path
import platform
import subprocess

ROOT = Path(__file__).resolve().parents[2]
TARGET = platform.machine() + '-' + platform.system().lower()
LIBRARY_EXTENSION = 'dylib' if platform.system() == 'Darwin' else 'so'


def command(runtime='c', *arguments, isolated=True, executable=None):
    """Build a launcher command; explicit executables are already runtime-specific."""
    if runtime not in ('c', 'chez'):
        raise ValueError(f'Unknown runtime: {runtime}')
    prefix = ([str(Path(executable).absolute())] if executable else
              [str(ROOT / 'otium'), '--runtime=' + runtime])
    return prefix + (['--no-project'] if isolated else []) + list(map(str, arguments))


def environment(*, runtime='c', stress=False, overrides=None):
    env = dict(os.environ)
    env.setdefault('OTIUM_FFI_LIBRARY', str(ROOT / 'shared/build' / TARGET /
                   ('libotium-ffi.' + LIBRARY_EXTENSION)))
    if stress and runtime == 'c':
        env['OTIUM_GC_STRESS'] = '1'
    if overrides:
        env.update(overrides)
    return env


def runtime_argument(parser, *, both=True):
    parser.add_argument('--runtime', choices=('c', 'chez', 'both') if both else ('c', 'chez'),
                        default='both' if both else 'c')


def runtimes(selection):
    return ('c', 'chez') if selection == 'both' else (selection,)


def metadata(commands, *, build_label='unspecified', compilation='launcher default; cache state uncontrolled'):
    def git(*args):
        result = subprocess.run(['git', '-C', str(ROOT), *args], text=True,
                                capture_output=True, check=False)
        return result.stdout.strip() if result.returncode == 0 else 'unknown'
    return {'created': datetime.datetime.now(datetime.timezone.utc).isoformat(),
            'commit': git('rev-parse', 'HEAD'), 'dirty': bool(git('status', '--porcelain')),
            'platform': platform.platform(), 'build_label': build_label,
            'compilation': compilation, 'commands': commands,
            'environment': {key: os.environ[key] for key in
                ('CHEZ', 'OTIUM_PLATFORM', 'OTIUM_BIN_ROOT', 'OTIUM_GC_STRESS') if key in os.environ}}


def run_directory(family):
    stamp = datetime.datetime.now(datetime.timezone.utc).strftime('%Y%m%dT%H%M%S.%fZ')
    return ROOT / 'benchmarks/runs' / family / (stamp + '-' + str(os.getpid()))
