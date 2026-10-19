#!/usr/bin/env python3

"""Fast correctness checks for the Otium benchmark ports."""

import argparse
import os
import shutil
import subprocess
import sys
import tempfile
from pathlib import Path

# Repository modules, for running this file directly.
sys.path[:0] = [str(Path(__file__).resolve().parents[2] / part) for part in ('', 'tools')]
from benchmarks.r7rs import run as RUNNER
from otium_tools import command


sys.dont_write_bytecode = True
ROOT = Path(__file__).resolve().parent
INPUTS = {
    "fib": "1\n10\n55\n",
    "ack": "1\n2\n4\n11\n",
    "tak": "1\n10\n6\n3\n4\n",
    "cpstak": "1\n10\n6\n3\n4\n",
    "takl": "1\n(8 7 6 5 4 3 2 1)\n(4 3 2 1)\n(2 1)\n3\n",
    "nqueens": "1\n8\n92\n",
    "destruc": (
        "1\n3\n2\n"
        "(() (1) (1) (1 ()) (1 ()) (1 ()) (1 ()) (1 ()) (1 ()) "
        "(1 1 () () ()))\n"
    ),
    "mperm": "1\n5\n2\n1\n0\n",
    "gcbench": "1\n8\n0\n",
    "quicksort": "1\n100\n1000\nignored\n",
    "fft": "1\n16\n0.0\n0.0\n",
    "equal": "4\n4\n4\n10\n10\n20\n#t\n",
}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("binary", type=Path, nargs="?")
    parser.add_argument("--runtime", choices=["c", "chez"], default="c")
    args = parser.parse_args()
    binary = command(args.runtime, isolated=False, executable=args.binary)
    flags = ["--no-project"]
    if args.runtime == "c":
        flags.append("--c.max-depth=2000")
    failures = 0
    if not RUNNER.vendor_available(ROOT):
        RUNNER.print_vendor_error()
        return 2
    deriv_input = (ROOT / "vendor" / "inputs" / "deriv.input").read_text()
    INPUTS["deriv"] = deriv_input.replace("10000000", "1", 1)

    with tempfile.TemporaryDirectory(prefix="otium-r7rs-test-") as temp_dir:
        for name, input_text in INPUTS.items():
            source = Path(temp_dir) / f"{name}.ot"
            source.write_text(RUNNER.otium_program(ROOT, name, "otium-test", input_text))
            result = subprocess.run(
                [
                    *binary,
                    *flags,
                    source,
                ],
                text=True,
                capture_output=True,
                check=False,
                timeout=60,
            )
            expected = "+!CSVLINE!+otium-test,"
            if result.returncode != 0 or expected not in result.stdout or "INCORRECT" in result.stdout:
                print(f"FAIL {name}", file=sys.stderr)
                print(result.stdout, file=sys.stderr)
                print(result.stderr, file=sys.stderr)
                failures += 1
            else:
                print(f"ok {name}")

        guile = shutil.which("guile")
        if guile:
            source = Path(temp_dir) / "fib-guile.scm"
            source.write_text(RUNNER.upstream_program(ROOT, "fib"))
            env = os.environ.copy()
            env["GUILE_AUTO_COMPILE"] = "0"
            result = subprocess.run(
                [guile, source],
                input=INPUTS["fib"],
                text=True,
                capture_output=True,
                check=False,
                timeout=60,
                env=env,
            )
            if result.returncode != 0 or "+!CSVLINE!+" not in result.stdout or "INCORRECT" in result.stdout:
                print("FAIL upstream guile workflow", file=sys.stderr)
                print(result.stdout, file=sys.stderr)
                print(result.stderr, file=sys.stderr)
                return 1
            print("ok upstream-guile")
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
