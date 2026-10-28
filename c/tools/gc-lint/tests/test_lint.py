"""Check otium-gc-lint against the fixtures in this directory.

Every line of a fixture that should be reported ends in a comment
"warn: KIND", and nothing else may be reported.
"""

import os
import re
import shutil
import signal
import subprocess
import sys
from pathlib import Path


def run(arguments):
    process = subprocess.Popen(
        arguments, text=True, stdout=subprocess.PIPE, stderr=subprocess.PIPE,
        start_new_session=True,
    )
    try:
        stdout, stderr = process.communicate(timeout=60)
    except subprocess.TimeoutExpired:
        os.killpg(process.pid, signal.SIGKILL)
        stdout, stderr = process.communicate()
        raise AssertionError(f"gc-lint took over 60 seconds: {arguments}\n{stdout}{stderr}")
    return process.returncode, stdout, stderr


tool, config, resource_dir = sys.argv[1:]
tests = Path(__file__).resolve().parent
# Homebrew's clang doesn't know where the macOS SDK is, so system headers
# need the same -isysroot the runner gives the runtime.
sysroot = []
if sys.platform == "darwin" and shutil.which("xcrun"):
    sysroot = ["-isysroot", subprocess.check_output(["xcrun", "--show-sdk-path"], text=True).strip()]
failed = False
for fixture in sorted(tests.glob("*.c")):
    expected = set()
    for number, line in enumerate(fixture.read_text().splitlines(), 1):
        if match := re.search(r"warn: ([a-z-]+)", line):
            expected.add((number, match.group(1)))
    status, stdout, stderr = run([
        tool, "--warnings-as-errors", f"--config={config}",
        f"--config={tests / 'gc-lint.conf'}", str(fixture), "--",
        f"-resource-dir={resource_dir}", "-std=c11", *sysroot,
    ])
    reported = set()
    for line in stdout.splitlines():
        if match := re.match(rf"{re.escape(str(fixture))}:(\d+):\d+: warning: .*\[otium-gc-([a-z-]+)\]", line):
            reported.add((int(match.group(1)), match.group(2)))
    missing, extra = sorted(expected - reported), sorted(reported - expected)
    if missing or extra or status != (1 if expected else 0):
        failed = True
        print(f"{fixture.name}: exit status {status}", file=sys.stderr)
        for number, kind in missing:
            print(f"  missing {kind} at line {number}", file=sys.stderr)
        for number, kind in extra:
            print(f"  unexpected {kind} at line {number}", file=sys.stderr)
        print(stdout + stderr, file=sys.stderr)
sys.exit(1 if failed else 0)
