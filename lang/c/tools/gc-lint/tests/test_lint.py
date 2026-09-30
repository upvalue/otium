import subprocess
import sys


tool, stale_fixture, barrier_fixture = sys.argv[1:]

result = subprocess.run(
    [tool, "--warnings-as-errors", "--report-suppressions", stale_fixture, "--", "-std=c23"],
    text=True,
    stdout=subprocess.PIPE,
    stderr=subprocess.PIPE,
)
output = result.stdout + result.stderr

assert result.returncode == 1, result
assert "raw GC pointer 'string' may be stale" in output, output
assert output.count("raw GC pointer 'string' may be stale") == 2, output
assert "accepted stale-borrow escape hatch" in output, output
assert "pinned by the embedding host" in output, output
assert "requires a nonempty reason" in output, output

result = subprocess.run(
    [tool, "--warnings-as-errors", "--report-suppressions", barrier_fixture, "--", "-std=c23"],
    text=True,
    stdout=subprocess.PIPE,
    stderr=subprocess.PIPE,
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
