#!/usr/bin/env python3
"""Open real hardware-rendered windows and test reload while frames advance."""
import argparse
import os
from pathlib import Path
import re
import shutil
import struct
import subprocess
import tempfile
import time

ROOT = Path(__file__).resolve().parents[1]


def status(path):
    if not path.exists():
        return {}
    return {key: (value[1:-1] if value.startswith('"') else int(value))
            for key, value in re.findall(r'\(([a-z-]+) ("[^"]*"|[0-9]+)\)', path.read_text())}


def wait_for(process, path, predicate, label):
    deadline = time.monotonic() + 15
    while time.monotonic() < deadline:
        sample = status(path)
        if sample and predicate(sample):
            return sample
        if process.poll() is not None:
            raise AssertionError(f"demo exited before {label}: {process.returncode}")
        time.sleep(0.025)
    raise AssertionError(f"timed out waiting for {label}: {status(path)}")


def replace(path, text):
    temporary = path.with_suffix(".tmp")
    temporary.write_text(text)
    os.replace(temporary, path)


def scene(revision, color, broken=False):
    draw = "(error 'render \"intentional test failure\")" if broken else (
        "(vector-set! state 0 (+ 1 (vector-ref state 0)))\n"
        f"  (clear! canvas 16 20 30) (rect! canvas 200 200 200 200 {color})")
    return f"""(ns scenes.shapes)
(chez/import (otium canvas))
(defonce state (vector 0))
(define revision "{revision}")
(define (render! canvas elapsed dt) {draw})
"""


def pixel(path, x, y):
    data = path.read_bytes()
    assert data[:2] == b"BM", "capture is not a BMP"
    offset = struct.unpack_from("<I", data, 10)[0]
    width, height = struct.unpack_from("<ii", data, 18)
    bits = struct.unpack_from("<H", data, 28)[0]
    compression = struct.unpack_from("<I", data, 30)[0]
    assert bits in (24, 32) and compression in (0, 3)
    row = ((width * bits + 31) // 32) * 4
    position = offset + (height - 1 - y if height > 0 else y) * row + x * (bits // 8)
    value = int.from_bytes(data[position:position + bits // 8], "little")
    masks = struct.unpack_from("<III", data, 54) if compression == 3 else (0xff0000, 0xff00, 0xff)
    return tuple((value & mask) >> ((mask & -mask).bit_length() - 1) for mask in masks)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--artifacts", type=Path,
                        help="preserve captures and logs in this directory")
    args = parser.parse_args()
    environment = os.environ.copy()
    if not environment.get("SDL3_LIBRARY"):
        libdir = subprocess.check_output(["pkg-config", "--variable=libdir", "sdl3"], text=True).strip()
        suffix = "dylib" if os.uname().sysname == "Darwin" else "so"
        environment["SDL3_LIBRARY"] = str(Path(libdir) / f"libSDL3.{suffix}")
    subprocess.run([environment.get("CHEZ", "chez"), "--libdirs", f"{ROOT}:{ROOT / '.deps'}",
                    "--script", str(ROOT / "sdl/test_events.ss")], env=environment,
                   check=True, timeout=20)
    with tempfile.TemporaryDirectory(prefix="otium-sdl-test-") as temporary:
        directory = Path(temporary)
        scenes = directory / "scenes"
        scenes.mkdir()
        source = scenes / "shapes.scm"
        state_path = directory / "status.scm"
        capture = directory / "reload.bmp"
        log = directory / "reload.log"
        replace(source, scene("initial", "255 0 0"))
        command = [str(ROOT / "sdl-demo"), "shapes", "--scene-root", str(directory),
                   "--frames", "360", "--status", str(state_path), "--capture", str(capture)]
        with log.open("w") as output:
            process = subprocess.Popen(command, stdout=output, stderr=subprocess.STDOUT)
            try:
                initial = wait_for(process, state_path, lambda s: s["scene-ticks"] >= 5, "first frames")
                assert initial["backend"] != "software"
                replace(source, scene("second", "0 255 0"))
                second = wait_for(process, state_path,
                    lambda s: s["revision"] == "second" and s["reloads"] == 1, "new renderer")
                assert second["scene-ticks"] > initial["scene-ticks"], "state reset on reload"
                replace(source, "(ns scenes.shapes)\n(")
                failed = wait_for(process, state_path, lambda s: s["reload-errors"] == 1, "syntax error")
                wait_for(process, state_path,
                    lambda s: s["scene-ticks"] > failed["scene-ticks"] + 3, "old render after failed read")
                replace(source, scene("broken-render", "0 0 0", broken=True))
                broken = wait_for(process, state_path, lambda s: s["render-errors"] == 1, "render error")
                wait_for(process, state_path, lambda s: s["frames"] > broken["frames"] + 3,
                         "event loop survives render error")
                replace(source, scene("recovered", "0 80 255"))
                recovered = wait_for(process, state_path,
                    lambda s: s["revision"] == "recovered" and s["reloads"] == 3
                              and s["scene-ticks"] > broken["scene-ticks"], "render recovery")
                for key in ("window", "renderer", "backend"):
                    assert recovered[key] == initial[key], f"{key} changed during reload"
                assert process.wait(timeout=15) == 0, log.read_text()
                final = status(state_path)
                assert final["frames"] == 360 and final["scene-ticks"] > recovered["scene-ticks"]
                assert pixel(capture, 250, 250) == (0, 80, 255), "GPU readback did not reflect reloaded draw code"
                print(f"PASS: {final['backend']}, same window/renderer, preserved state, "
                      "syntax-error and render-error recovery, GPU pixel readback")
            except Exception:
                print(log.read_text())
                raise
            finally:
                if process.poll() is None:
                    process.terminate()
                    try:
                        process.wait(timeout=3)
                    except subprocess.TimeoutExpired:
                        process.kill()
                        process.wait()
        for name in ("shapes", "particles"):
            target = directory / f"{name}.bmp"
            result = subprocess.run([str(ROOT / "sdl-demo"), name, "--frames", "30",
                                     "--capture", str(target)],
                                    capture_output=True, text=True, timeout=20)
            (directory / f"{name}.log").write_text(result.stdout + result.stderr)
            assert result.returncode == 0, result.stdout + result.stderr
            assert "reload errors 0, render errors 0" in result.stdout, result.stdout
            assert target.stat().st_size > 1000
            print(f"PASS: {name} demo, 30 frames and capture")
        if args.artifacts:
            args.artifacts.mkdir(parents=True, exist_ok=True)
            for path in directory.iterdir():
                if path.is_file():
                    shutil.copy2(path, args.artifacts / path.name)


if __name__ == "__main__":
    main()
