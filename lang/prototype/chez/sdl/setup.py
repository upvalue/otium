#!/usr/bin/env python3
"""Install a checked, pinned chez-sdl3 source dependency locally."""
import argparse
import hashlib
from pathlib import Path
import os
import subprocess
import tarfile
import urllib.request

ROOT = Path(__file__).resolve().parents[1]
REVISION = "d7374ede2c8485e673e106bf054531cf5cc1e6df"
SHA256 = "f28f233f8b93d681ec507fda2ae7d430f162520c8048acc23164628f2ae50fa9"
EXPORTS = """
SDL_Init SDL_Quit SDL_GetError SDL_CreateWindow SDL_DestroyWindow
SDL_CreateRenderer SDL_DestroyRenderer SDL_GetRendererName SDL_SetRenderVSync
SDL_SetRenderLogicalPresentation SDL_SetRenderDrawColor SDL_RenderClear
SDL_RenderPresent SDL_RenderFillRect SDL_RenderLine SDL_RenderDebugText
SDL_RenderReadPixels SDL_SaveBMP SDL_DestroySurface SDL_SetWindowTitle
SDL_PollEvent SDL_PushEvent SDL_Event SDL_KeyboardEvent SDL_FRect SDL_Rect
SDL_INIT_VIDEO SDL_WINDOW_RESIZABLE SDL_LOGICAL_PRESENTATION_LETTERBOX
SDL_EVENT_QUIT SDL_EVENT_WINDOW_CLOSE_REQUESTED SDL_EVENT_KEY_DOWN
SDL_SCANCODE_ESCAPE SDL_SCANCODE_R
"""


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--archive", type=Path, help="use a cached upstream archive")
    args = parser.parse_args()
    destination = ROOT / ".deps/chez-sdl3"
    archive = ROOT / ".deps/chez-sdl3.tar.gz"
    archive.parent.mkdir(exist_ok=True)
    if args.archive:
        data = args.archive.read_bytes()
    elif archive.exists():
        data = archive.read_bytes()
    else:
        url = f"https://codeload.github.com/melted/chez-sdl3/tar.gz/{REVISION}"
        with urllib.request.urlopen(url, timeout=60) as response:
            data = response.read()
    if hashlib.sha256(data).hexdigest() != SHA256:
        raise SystemExit("chez-sdl3 archive checksum mismatch")
    archive.write_bytes(data)
    prefix = f"chez-sdl3-{REVISION}/"
    with tarfile.open(archive) as package:
        for member in package.getmembers():
            relative = member.name.removeprefix(prefix)
            if not member.name.startswith(prefix) or not member.isfile():
                continue
            if relative != "LICENSE" and not (relative.startswith("src/") and relative.endswith(".scm")):
                continue
            if ".." in Path(relative).parts:
                raise SystemExit("invalid archive member")
            path = destination / relative
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_bytes(package.extractfile(member).read())
    events = destination / "src/include/SDL_events.scm"
    text = events.read_text()
    before = "(which SDL_KeyboardID)\n    (key SDL_Keycode)"
    if text.count(before) != 1:
        raise SystemExit("keyboard ABI patch did not match")
    events.write_text(text.replace(before,
        "(which SDL_KeyboardID)\n    ;; Local ABI fix: physical scancode precedes key.\n"
        "    (scancode SDL_Scancode)\n    (key SDL_Keycode)"))
    exports = ROOT / ".deps/sdl-exports.scm"
    exports.write_text("(" + EXPORTS + ")\n")
    subprocess.run([os.environ.get("CHEZ", "chez"), "--script",
                    str(ROOT / "sdl/wrap.ss"), str(destination / "src"),
                    str(exports), str(ROOT / ".deps/sdl3.sls")], check=True)
    print(f"Installed chez-sdl3 {REVISION} in {destination}")


if __name__ == "__main__":
    main()
