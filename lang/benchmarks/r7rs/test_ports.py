#!/usr/bin/env python3

"""Fast correctness checks for the Otium benchmark ports."""

import importlib.util
import os
import shutil
import subprocess
import sys
import tempfile
from pathlib import Path


sys.dont_write_bytecode = True
ROOT = Path(__file__).resolve().parent
SPEC = importlib.util.spec_from_file_location("r7rs_runner", ROOT / "run.py")
RUNNER = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(RUNNER)

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
}


def main():
    binary = Path(sys.argv[1]).resolve()
    if not RUNNER.vendor_available(ROOT):
        RUNNER.print_vendor_error()
        return 2
    deriv_input = (ROOT / "vendor" / "inputs" / "deriv.input").read_text()
    INPUTS["deriv"] = deriv_input.replace("10000000", "1", 1)

    with tempfile.TemporaryDirectory(prefix="otium-r7rs-test-") as temp_dir:
        for name, input_text in INPUTS.items():
            source = Path(temp_dir) / f"{name}.scm"
            source.write_text(RUNNER.otium_program(ROOT, name, "otium-test", input_text))
            result = subprocess.run(
                [
                    binary,
                    "--max-depth",
                    "2000",
                    "--stack-slots",
                    "32768",
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
                return 1
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
    return 0


if __name__ == "__main__":
    sys.exit(main())
