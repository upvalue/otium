#!/usr/bin/env python3
"""Run the portable SDL scene host or acceptance program ."""
import argparse
import os
from pathlib import Path
import platform
import subprocess
import sys
import tempfile

# Repository modules, for running this file directly.
sys.path[:0] = [str(Path(__file__).resolve().parents[1] / part) for part in ('', 'tools')]
from native.sdl.host import bootstrap, command, environment, literal, DEFAULT_MANIFEST
ROOT = Path(__file__).resolve().parents[1]

def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--manifest', type=Path, default=DEFAULT_MANIFEST)
    parser.add_argument('--scene', choices=('shapes','particles'), default='shapes')
    parser.add_argument('--scene-path', type=Path)
    parser.add_argument('--frames', type=int, default=0)
    parser.add_argument('--capture', type=Path)
    parser.add_argument('--status', type=Path, help='Atomically write frame/reload counters and resource identity')
    parser.add_argument('--renderer', default='metal' if platform.system() == 'Darwin' else 'opengl,opengles2,vulkan')
    parser.add_argument('--audio', choices=('off','dummy','real'), default='off')
    parser.add_argument('--acceptance', action='store_true')
    parser.add_argument('--inject-error', action='store_true')
    args = parser.parse_args()
    if args.frames < 0: parser.error('--frames must be nonnegative')
    if args.capture and not args.frames and not args.acceptance: parser.error('--capture requires --frames')
    source = bootstrap(args.manifest)
    source += f'''\n(def renderer-name {literal(args.renderer)})
(def frame-limit {args.frames})
(def capture-path {literal(args.capture.resolve()) if args.capture else '#f'})
(def status-path {literal(args.status.resolve()) if args.status else '#f'})
(def audio-enabled? {'#f' if args.audio == 'off' else '#t'})
(def inject-error? {'#t' if args.inject_error else '#f'})
'''
    if args.acceptance:
        source += (ROOT/'examples/sdl-acceptance.ot').read_text()
    else:
        names = '("/usr/lib/libSystem.B.dylib")' if platform.system() == 'Darwin' else '("libc.so.6")'
        source += "(def host-libc-names '"+names+")\n"
        scene = args.scene_path or ROOT/'examples/scenes'/(args.scene+'.ot')
        source += '(def scene-path '+literal(scene.resolve())+')\n'
        source += (ROOT/'native/sdl/reload.ot').read_text()
        source += (ROOT/'examples/sdl-host.ot').read_text()
    env = environment()
    if args.audio == 'dummy': env['SDL_AUDIO_DRIVER'] = 'dummy'
    if args.audio == 'real' and env.get('SDL_AUDIO_DRIVER') == 'dummy':
        parser.error('--audio real cannot use SDL_AUDIO_DRIVER=dummy')
    with tempfile.TemporaryDirectory(prefix='otium-sdl-') as directory:
        script = Path(directory)/'host.ot'; script.write_text(source)
        return subprocess.run(command(script), cwd=ROOT, env=env).returncode

if __name__ == '__main__':
    raise SystemExit(main())
