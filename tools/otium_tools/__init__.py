"""Repository execution helpers. Runtime behavior belongs in the launchers."""
import datetime
import json
import os
from pathlib import Path
import platform
import subprocess

ROOT = Path(__file__).resolve().parents[2]
TARGET = platform.machine() + '-' + platform.system().lower()
LIBRARY_EXTENSION = 'dylib' if platform.system() == 'Darwin' else 'so'
BUILD = Path(os.environ.get('OTIUM_BUILD', ROOT / 'build' / TARGET / 'default'))


def command(*arguments, isolated=True, executable=None):
    """Build a launcher command, or one for an explicit executable."""
    prefix = [str(Path(executable).absolute() if executable else ROOT / 'otium')]
    return prefix + (['--no-project'] if isolated else []) + list(map(str, arguments))


def environment(*, stress=False, overrides=None):
    env = dict(os.environ)
    env.setdefault('OTIUM_FFI_LIBRARY', str(BUILD / ('libotium-ffi.' + LIBRARY_EXTENSION)))
    if stress:
        env['OTIUM_GC_STRESS'] = '1'
    if overrides:
        env.update(overrides)
    return env


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
                ('OTIUM_BUILD', 'OTIUM_GC_STRESS') if key in os.environ}}


def run_directory(family):
    stamp = datetime.datetime.now(datetime.timezone.utc).strftime('%Y%m%dT%H%M%S.%fZ')
    return ROOT / 'benchmarks/runs' / family / (stamp + '-' + str(os.getpid()))
