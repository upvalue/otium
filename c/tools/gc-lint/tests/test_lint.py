import os
from pathlib import Path
import signal
import subprocess
import sys


def run_lint(arguments):
    process = subprocess.Popen(
        arguments, text=True, stdout=subprocess.PIPE, stderr=subprocess.PIPE,
        start_new_session=True,
    )
    try:
        stdout, stderr = process.communicate(timeout=10)
    except subprocess.TimeoutExpired:
        os.killpg(process.pid, signal.SIGKILL)
        stdout, stderr = process.communicate()
        raise AssertionError(f"GC lint exceeded 10 seconds: {arguments}\n{stdout}{stderr}")
    return subprocess.CompletedProcess(arguments, process.returncode, stdout, stderr)


tool, stale_fixture, barrier_fixture = sys.argv[1:]

result = run_lint(
    [tool, "--warnings-as-errors", "--report-suppressions", stale_fixture, "--", "-std=c23"],
)
output = result.stdout + result.stderr

assert result.returncode == 1, result
assert "raw GC pointer 'string' may be stale" in output, output
assert output.count("raw GC pointer 'string' may be stale") == 2, output
assert "accepted stale-borrow escape hatch" in output, output
assert "pinned by the embedding host" in output, output
assert "requires a nonempty reason" in output, output

result = run_lint(
    [tool, "--warnings-as-errors", "--report-suppressions", barrier_fixture, "--", "-std=c23"],
)
output = result.stdout + result.stderr

assert result.returncode == 1, result
assert output.count("managed heap edge is written without ot_store") == 6, output
assert "owner type 'ot_pair_obj' does not contain the target edge" in output, output
assert "ot_store target is not a managed heap edge" in output, output
assert "bulk write with 'memcpy'" in output, output
assert "accepted direct-store escape hatch" in output, output
assert "object is outside the managed heap" in output, output
assert "OT_GC_ALLOW_DIRECT_STORE requires a nonempty reason" in output, output

convergence_fixture = str(Path(stale_fixture).with_name("convergence.c"))
result = run_lint([tool, "--warnings-as-errors", convergence_fixture, "--", "-std=c23"])
output = result.stdout + result.stderr
assert result.returncode == 1, result
assert output.count("raw GC pointer 'string' may be stale") == 2, output
assert output.count("raw GC pointer 'mixed' may be stale") == 2, output
assert "raw GC pointer 'refreshed'" not in output, output
assert output.count("possibly allocating call to 'grow_left'") == 2, output
assert output.count("possibly allocating call to 'grow_right'") == 2, output
