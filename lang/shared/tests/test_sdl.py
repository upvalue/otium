#!/usr/bin/env python3
"""SDL manifest, copied events, dummy audio, and accelerated acceptance checks."""
import argparse
import json
import os
from pathlib import Path
import platform
import shlex
import subprocess
import struct
import sys
import tempfile
import time
ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT/'shared/sdl'))
from host import bootstrap, command, environment, literal, DEFAULT_MANIFEST

def run_source(runtime, source, directory, env=None):
    script = directory/(runtime+'.scm'); script.write_text(source)
    return subprocess.run(command(runtime, script), cwd=ROOT, env=env or environment(), capture_output=True, text=True, timeout=45)

def passed(result, marker):
    assert result.returncode == 0 and marker in result.stdout, (result.returncode,result.stdout,result.stderr)

def pixel(path, x, y):
    data = path.read_bytes()
    assert data[:2] == b'BM'
    offset = struct.unpack_from('<I', data, 10)[0]
    width, height = struct.unpack_from('<ii', data, 18)
    bits = struct.unpack_from('<H', data, 28)[0]
    compression = struct.unpack_from('<I', data, 30)[0]
    assert bits in (24, 32) and compression in (0, 3)
    stride = ((width * bits + 31) // 32) * 4
    row = height - 1 - y if height > 0 else y
    position = offset + row * stride + x * (bits // 8)
    value = int.from_bytes(data[position:position + bits // 8], 'little')
    masks = struct.unpack_from('<III', data, 54) if compression == 3 else (0xff0000, 0xff00, 0xff)
    return tuple((value & mask) >> ((mask & -mask).bit_length() - 1) for mask in masks)


def replace(path, source):
    temporary = path.with_suffix('.new')
    temporary.write_text(source)
    temporary.replace(path)


def wait_status(process, path, predicate):
    deadline = time.monotonic() + 20
    sample = None
    while time.monotonic() < deadline:
        if path.exists():
            values = list(map(int, path.read_text().split()))
            sample = dict(zip(('frames', 'ticks', 'reloads', 'reload_errors',
                               'render_errors', 'same_window', 'same_renderer'), values))
            assert sample['same_window'] == sample['same_renderer'] == 1, sample
            if predicate(sample):
                return sample
        if process.poll() is not None:
            raise AssertionError(('SDL host exited before expected status', sample))
        time.sleep(0.025)
    raise AssertionError(('Timed out waiting for SDL status', sample))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--runtime', choices=('c','chez','both'), default='both')
    parser.add_argument('--require-accelerated', action='store_true')
    args = parser.parse_args()
    subprocess.run([sys.executable,str(ROOT/'shared/sdl/setup.py'),'--check'], check=True)
    runtimes = ('c','chez') if args.runtime == 'both' else (args.runtime,)
    with tempfile.TemporaryDirectory(prefix='otium-sdl-tests-') as temp:
        directory = Path(temp)
        fixture = directory/('fixture.'+('dylib' if platform.system() == 'Darwin' else 'so'))
        flags = shlex.split(subprocess.check_output(['pkg-config','--cflags','sdl3'],text=True))
        subprocess.run(shlex.split(os.environ.get('CC','cc'))+['-std=c11','-Wall','-Wextra','-Werror','-fPIC','-dynamiclib' if platform.system() == 'Darwin' else '-shared']+flags+[str(ROOT/'shared/tests/sdl/fixture.c'),'-o',str(fixture)],check=True)
        for runtime in runtimes:
            prefix = bootstrap(runtime)
            env = environment(); env['SDL_AUDIO_DRIVER'] = 'dummy'
            result = run_source(runtime,prefix+'\n(def fixture-path '+literal(fixture)+')\n'+(ROOT/'shared/tests/sdl/common.scm').read_text(),directory,env)
            passed(result,'SDL fixture checks: 15')
            print(runtime+': manifest, copied event lifetime, input conversion, dummy audio passed',flush=True)
            # Alter the manifest itself, so startup must reject a layout before SDL_Init.
            bad = json.loads(DEFAULT_MANIFEST.read_text()); bad['layout']['SDL_Event']['size'] -= 8
            manifest = directory/'bad.json'; manifest.write_text(json.dumps(bad))
            mismatch = run_source(runtime,bootstrap(runtime,manifest),directory)
            assert mismatch.returncode != 0 and 'SDL header layout mismatch' in mismatch.stderr, (mismatch.stdout,mismatch.stderr)
            invalid_env = dict(env,SDL3_LIBRARY='/no/such/SDL3-library')
            old = os.environ.get('SDL3_LIBRARY'); os.environ['SDL3_LIBRARY'] = invalid_env['SDL3_LIBRARY']
            try: override = bootstrap(runtime)
            finally:
                if old is None: del os.environ['SDL3_LIBRARY']
                else: os.environ['SDL3_LIBRARY'] = old
            result = run_source(runtime,override,directory,env)
            assert result.returncode != 0 and '/no/such/SDL3-library' in result.stderr, (result.stdout,result.stderr)
            launch = [sys.executable,str(ROOT/'examples/shared/run-sdl.py'),'--runtime',runtime]
            result = subprocess.run(launch+['--acceptance','--audio','dummy','--inject-error','--capture',str(directory/(runtime+'.bmp'))],env=env,capture_output=True,text=True,timeout=45)
            unavailable = any(text in result.stderr for text in ('The video driver did not add any displays','No available video device','Unable to open a console terminal','Could not initialize EGL','Could not find a valid EGL device','No available renderers','Couldn\'t find matching render driver'))
            if result.returncode and unavailable and not args.require_accelerated:
                print(runtime+': SKIP accelerated graphics (no display/backend): '+result.stderr.strip(),flush=True)
                continue
            passed(result,'SDL acceptance checks: 21')
            assert 'SDL initialized: 0' in result.stdout and 'Foreign allocations: 0' in result.stdout
            assert (directory/(runtime+'.bmp')).stat().st_size > 100
            print(runtime+': accelerated pixels, texture, injected input, reload identity and error teardown passed',flush=True)
            partial = prefix + """
(def failed? #f)
(try (unwind-protect (sdl-open! "otium-invalid-renderer" #f) (sdl-close!))
  (catch ((fn (c) #t) c) (set! failed? #t)))
(if (and failed? (equal? sdl-resources '()) (= 0 (SDL_WasInit 0))
         (= 0 (ffi/allocation-count)))
    (display "Partial startup cleanup passed") (error "Partial startup leaked"))
"""
            passed(run_source(runtime, partial, directory, env), 'Partial startup cleanup passed')
            for scene in ('shapes','particles'):
                result = subprocess.run(launch+['--scene',scene,'--frames','3'],env=env,capture_output=True,text=True,timeout=45)
                passed(result,'render errors 0; ticks 3')
            # Synchronize edits with observed frames, not interpreter startup time.
            scene = directory/'scene.scm'
            status = directory/(runtime+'-status')
            capture = directory/(runtime+'-reload.bmp')
            replace(scene, '(def (render! t dt) (set! scene-ticks (+ scene-ticks 1)) (clear! 10 20 30))\n')
            process = subprocess.Popen(launch+['--scene-path',str(scene),'--frames','180',
                                               '--status',str(status),'--capture',str(capture)],
                                       env=env,stdout=subprocess.PIPE,stderr=subprocess.PIPE,text=True)
            try:
                initial = wait_status(process, status, lambda s: s['ticks'] >= 3)
                replace(scene, '(def (render!\n')
                failed = wait_status(process, status, lambda s: s['reload_errors'] == 1)
                continued = wait_status(process, status, lambda s: s['ticks'] > failed['ticks'] + 3)
                assert continued['ticks'] > initial['ticks']
                replace(scene, '(def (render! t dt) (error "test render"))\n')
                broken = wait_status(process, status, lambda s: s['render_errors'] == 1)
                wait_status(process, status, lambda s: s['frames'] > broken['frames'] + 3)
                replace(scene, '(def (render! t dt) (set! scene-ticks (+ scene-ticks 1)) (clear! 30 40 50))\n')
                wait_status(process, status, lambda s: s['reloads'] == 3 and s['ticks'] > broken['ticks'])
                stdout,stderr = process.communicate(timeout=30)
            except Exception:
                process.terminate()
                stdout,stderr = process.communicate(timeout=5)
                print(stdout, stderr)
                raise
            finally:
                if process.poll() is None: process.kill(); process.wait()
            assert process.returncode == 0 and 'Foreign allocations: 0' in stdout, (stdout,stderr)
            assert pixel(capture, 250, 250) == (30, 40, 50), 'Reloaded draw code was not captured'
            print(runtime+': shapes, particles, watched syntax/render error recovery passed',flush=True)

if __name__ == '__main__': main()
