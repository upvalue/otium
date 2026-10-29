"""Process timing shared by the benchmark runners."""
import os
from pathlib import Path
import re
import shutil
import signal
import subprocess
import sys
import tempfile
import threading
import time

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'tools'))
from otium_tools import BUILD

MAXRSS_SOURCE = Path(__file__).resolve().parent / 'maxrss.c'
MAXRSS = BUILD / 'bench-maxrss'


def progress(index, total, what, status, wall):
    """Print one sample's outcome as it finishes, even when output is a file."""
    took = f' {wall:.2f}s' if isinstance(wall, float) else ''
    print(f'[{index + 1}/{total}] {what}: {status}{took}', flush=True)


def choose_cpu(option):
    """The CPU for --cpu: 'none', 'auto' (the last allowed CPU) or a number."""
    if option == 'none':
        return None
    if not shutil.which('taskset'):
        print('warning: taskset not found; running unpinned', file=sys.stderr)
        return None
    if option == 'auto':
        return max(os.sched_getaffinity(0))
    return int(option)


def pinned(command, cpu):
    """Prefix command with taskset so it runs on one CPU, when cpu is given."""
    if cpu is None:
        return list(command)
    return ['taskset', '-c', str(cpu), *command]


def maxrss_helper():
    """Path to the compiled maxrss helper, building it if needed; None without cc."""
    if MAXRSS.is_file() and MAXRSS.stat().st_mtime >= MAXRSS_SOURCE.stat().st_mtime:
        return MAXRSS
    compiler = os.environ.get('CC') or shutil.which('cc')
    if not compiler:
        return None
    MAXRSS.parent.mkdir(parents=True, exist_ok=True)
    result = subprocess.run([compiler, '-O2', '-o', str(MAXRSS), str(MAXRSS_SOURCE)],
                            capture_output=True, text=True)
    if result.returncode != 0:
        print(f'warning: could not build {MAXRSS}: {result.stderr}', file=sys.stderr)
        return None
    return MAXRSS


def execute(command, timeout, stdin=None, env=None, cwd=None, binary=False):
    """Run command once and measure it.

    Returns (code, wall_seconds, stdout, stderr, max_rss_kb). code is the exit
    status, or "timeout". max_rss_kb comes from benchmarks/maxrss.c and is None
    when no C compiler is available to build it. Output goes through temporary
    files so a large output can't fill a pipe. stdout is bytes when binary is
    true.
    """
    helper = maxrss_helper()
    with tempfile.TemporaryDirectory(prefix='otium-bench-') as temp:
        temp = Path(temp)
        rss_path = temp / 'maxrss'
        full = [str(helper), str(rss_path), *command] if helper else list(command)
        (temp / 'stdin').write_bytes(stdin.encode() if isinstance(stdin, str) else (stdin or b''))
        with open(temp / 'stdin', 'rb') as inp, open(temp / 'stdout', 'wb') as out, \
                open(temp / 'stderr', 'wb') as err:
            started = time.perf_counter()
            # A new session lets a timeout kill the helper and the command together.
            process = subprocess.Popen(full, stdin=inp, stdout=out, stderr=err, env=env, cwd=cwd,
                                       start_new_session=True)
            expired = threading.Event()

            def kill():
                expired.set()
                os.killpg(process.pid, signal.SIGKILL)
            timer = threading.Timer(timeout, kill)
            timer.start()
            try:
                process.wait()
            finally:
                timer.cancel()
            wall = time.perf_counter() - started
        stdout = (temp / 'stdout').read_bytes()
        stderr = (temp / 'stderr').read_text(errors='replace')
        rss = None
        if helper and rss_path.exists() and rss_path.read_text().strip():
            rss = int(rss_path.read_text())
            # ru_maxrss is in bytes on macOS.
            if sys.platform == 'darwin':
                rss //= 1024
    if not binary:
        stdout = stdout.decode(errors='replace')
    if expired.is_set():
        return 'timeout', timeout, stdout, stderr, rss
    return process.returncode, wall, stdout, stderr, rss


def version(command, pattern=r'\d+(?:\.\d+)+\S*'):
    """First version-looking string printed by command, or None if it fails."""
    if not shutil.which(command[0]):
        return None
    try:
        result = subprocess.run(command, capture_output=True, text=True, timeout=30)
    except (OSError, subprocess.TimeoutExpired):
        return None
    match = re.search(pattern, result.stdout + result.stderr)
    return match.group(0) if match else 'unknown'
