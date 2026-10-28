"""SDL manifest, copied events, dummy audio, and accelerated rendering and reload."""
import json
import os
import sys

from harness import ROOT, Skip, defaults, library, test
from native.sdl.host import DEFAULT_MANIFEST, bootstrap, literal
from ui_capture import pixel

defaults(suite='native', needs=['ui'], timeout=120, parallel=False)
AUDIO = {'SDL_AUDIO_DRIVER': 'dummy'}
UNAVAILABLE = ('The video driver did not add any displays', 'No available video device',
               'Unable to open a console terminal', 'Could not initialize EGL',
               'Could not find a valid EGL device', 'No available renderers',
               "Couldn't find matching render driver")


def program(t, source, env=AUDIO):
    script = t.scratch / 'program.ot'
    script.write_text(source)
    return t.otium(script, env=env, timeout=45)


def host(t, *options, required=False):
    """Run examples/run_sdl.py, skipping when there is no display unless required."""
    result = t.run([sys.executable, ROOT / 'examples/run_sdl.py', *options],
                   env=AUDIO, timeout=45)
    if result.status and not required and any(text in result.stderr for text in UNAVAILABLE):
        raise Skip('no display or accelerated renderer: ' + result.stderr.strip())
    return result


@test('sdl manifest')
def manifest(t):
    t.expect(t.run([sys.executable, ROOT / 'native/sdl/setup.py', '--check']))


@test('sdl fixture')
def fixture(t):
    source = (bootstrap() + f'\n(def fixture-path {literal(library("sdl-fixture"))})\n' +
              (ROOT / 'tests/sdl/common.ot').read_text())
    t.expect(program(t, source), contains=['sdl fixture: 15 checks passed'])


@test('sdl layout mismatch')
def layout_mismatch(t):
    """Startup rejects a manifest whose layout differs from the headers, before SDL_Init."""
    bad = json.loads(DEFAULT_MANIFEST.read_text())
    bad['layout']['SDL_Event']['size'] -= 8
    manifest = t.scratch / 'bad.json'
    manifest.write_text(json.dumps(bad))
    t.expect(program(t, bootstrap(manifest)), status=None,
             stderr=['SDL header layout mismatch'])


@test('sdl library override')
def library_override(t):
    missing = '/no/such/SDL3-library'
    previous = os.environ.get('SDL3_LIBRARY')
    os.environ['SDL3_LIBRARY'] = missing
    try:
        source = bootstrap()
    finally:
        if previous is None:
            del os.environ['SDL3_LIBRARY']
        else:
            os.environ['SDL3_LIBRARY'] = previous
    t.expect(program(t, source, env=dict(AUDIO, SDL3_LIBRARY=missing)), status=None, stderr=[missing])


PARTIAL_STARTUP = """
(def failed? #f)
(try (unwind-protect (sdl-open! "otium-invalid-renderer" #f) (sdl-close!))
  (catch ((fn (c) #t) c) (set! failed? #t)))
(if (and failed? (equal? sdl-resources '()) (= 0 (SDL_WasInit 0))
         (= 0 (ffi/allocation-count)))
    (display "Partial startup cleanup passed") (error "Partial startup leaked"))
"""


def acceptance(t, required):
    capture = t.scratch / 'acceptance.bmp'
    result = host(t, '--acceptance', '--audio', 'dummy', '--inject-error', '--capture', capture,
                  required=required)
    t.expect(result, contains=['SDL acceptance checks: 21', 'SDL initialized: 0', 'Foreign allocations: 0'])
    t.check(capture.stat().st_size > 100, 'capture is empty')
    t.expect(program(t, bootstrap() + PARTIAL_STARTUP), contains=['Partial startup cleanup passed'])
    for scene in ('shapes', 'particles'):
        t.expect(host(t, '--scene', scene, '--frames', '3', required=required),
                 contains=['render errors 0; ticks 3'])


def replace(path, source):
    temporary = path.with_suffix('.new')
    temporary.write_text(source)
    temporary.replace(path)


def reload(t, required):
    """Edits are synchronized with observed frames, not interpreter startup time."""
    host(t, '--scene', 'shapes', '--frames', '1', required=required)
    scene, status, capture = t.scratch / 'scene.ot', t.scratch / 'status', t.scratch / 'reload.bmp'
    replace(scene, '(def (render! t dt) (set! scene-ticks (+ scene-ticks 1)) (clear! 10 20 30))\n')
    process = t.spawn([sys.executable, ROOT / 'examples/run_sdl.py',
                       '--scene-path', scene, '--frames', '180', '--status', status, '--capture', capture],
                      env=AUDIO, text=True, stdin=None)

    def sample(predicate):
        def read():
            if not status.exists():
                return None
            values = list(map(int, status.read_text().split()))
            current = dict(zip(('frames', 'ticks', 'reloads', 'reload_errors', 'render_errors',
                                'same_window', 'same_renderer'), values))
            t.check(current['same_window'] == current['same_renderer'] == 1, 'window or renderer replaced', current)
            return current if predicate(current) else None
        return t.wait(read, timeout=20, process=process, what='SDL status')

    initial = sample(lambda s: s['ticks'] >= 3)
    replace(scene, '(def (render!\n')
    failed = sample(lambda s: s['reload_errors'] == 1)
    continued = sample(lambda s: s['ticks'] > failed['ticks'] + 3)
    t.check(continued['ticks'] > initial['ticks'], 'scene stopped after a syntax error')
    replace(scene, '(def (render! t dt) (error "test render"))\n')
    broken = sample(lambda s: s['render_errors'] == 1)
    sample(lambda s: s['frames'] > broken['frames'] + 3)
    replace(scene, '(def (render! t dt) (set! scene-ticks (+ scene-ticks 1)) (clear! 30 40 50))\n')
    sample(lambda s: s['reloads'] == 3 and s['ticks'] > broken['ticks'])
    stdout, stderr = process.communicate(timeout=30)
    t.check(process.returncode == 0 and 'Foreign allocations: 0' in stdout, stdout, stderr)
    t.check(pixel(capture, 250, 250) == (30, 40, 50), 'reloaded draw code was not captured')


for required, suffix, suite in ((False, '', 'native'), (True, ' required', 'gpu')):
    test('sdl acceptance' + suffix, suite=suite)(
        lambda t, required=required: acceptance(t, required))
    test('sdl reload' + suffix, suite=suite)(
        lambda t, required=required: reload(t, required))
