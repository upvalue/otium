#!/usr/bin/env python3
"""Run the portable SDL scene host or acceptance program on C or Chez."""
import argparse
import os
from pathlib import Path
import platform
import subprocess
import sys
import tempfile
ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT/'shared/sdl'))
from host import bootstrap, command, environment, literal, DEFAULT_MANIFEST

def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--runtime', choices=('c','chez'), default='c')
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
    source = bootstrap(args.runtime, args.manifest)
    source += f'''\n(def renderer-name {literal(args.renderer)})
(def frame-limit {args.frames})
(def capture-path {literal(args.capture.resolve()) if args.capture else '#f'})
(def status-path {literal(args.status.resolve()) if args.status else '#f'})
(def audio-enabled? {'#f' if args.audio == 'off' else '#t'})
(def inject-error? {'#t' if args.inject_error else '#f'})
'''
    if args.acceptance:
        source += (ROOT/'examples/shared/sdl-acceptance.scm').read_text()
    else:
        names = '("/usr/lib/libSystem.B.dylib")' if platform.system() == 'Darwin' else '("libc.so.6")'
        source += "(def host-libc-names '"+names+")\n"
        scene = args.scene_path or ROOT/'examples/shared/scenes'/(args.scene+'.scm')
        source += '(def scene-path '+literal(scene.resolve())+')\n'
        source += (ROOT/'shared/sdl/reload.scm').read_text()
        source += (ROOT/'examples/shared/sdl-host.scm').read_text()
    env = environment()
    if args.audio == 'dummy': env['SDL_AUDIO_DRIVER'] = 'dummy'
    if args.audio == 'real' and env.get('SDL_AUDIO_DRIVER') == 'dummy':
        parser.error('--audio real cannot use SDL_AUDIO_DRIVER=dummy')
    with tempfile.TemporaryDirectory(prefix='otium-sdl-') as directory:
        script = Path(directory)/'host.scm'; script.write_text(source)
        return subprocess.run(command(args.runtime, script), cwd=ROOT, env=env).returncode

if __name__ == '__main__':
    raise SystemExit(main())
