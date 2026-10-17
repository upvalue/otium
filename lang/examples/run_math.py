#!/usr/bin/env python3
"""Compatibility entry point for the directly runnable math example."""
import argparse
import subprocess
from otium_tools import ROOT, command, environment, runtime_argument


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    runtime_argument(parser, both=False)
    args = parser.parse_args()
    return subprocess.run(command(args.runtime, ROOT / 'examples/math.ot'),
                          cwd=ROOT, env=environment()).returncode


if __name__ == '__main__':
    raise SystemExit(main())
