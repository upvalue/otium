import subprocess
import sys


tool, fixture = sys.argv[1:]
result = subprocess.run(
    [tool, "--warnings-as-errors", "--report-suppressions", fixture, "--", "-std=c23"],
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
