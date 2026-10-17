#!/usr/bin/env python3
"""Capture GPU scenes on both runtimes and check layout, pixels and teardown."""
import argparse
import json
import os
from pathlib import Path
import subprocess
import platform
from shared.tests import ui_capture as runner

from otium_tools import runtime_argument, runtimes, command as runtime_command, environment

ROOT = Path(__file__).resolve().parents[2]
parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--require-gpu', action='store_true')
runtime_argument(parser)
args = parser.parse_args()


def launch(runtime, *options, headless=True):
    env = environment()
    command = runtime_command(runtime) + [ '-L', str(ROOT/'shared/lib'), '-L', str(ROOT/'examples'),
               '-L', str(ROOT/'shared/tests'), str(ROOT/'examples/ui.ot'), '--',
               *(['--headless'] if headless else []), *map(str, options)]
    return subprocess.run(command, cwd=ROOT, env=env, capture_output=True, text=True, timeout=90)


def passed(result):
    assert result.returncode == 0, (result.stdout, result.stderr)
    assert 'UI-CLEANUP 0 0 0 0' in result.stdout, result.stdout


def pixel(rows, x, y):
    return tuple(rows[y][x*3:x*3+3])


# Captures remain available for inspection, including after a failed assertion.
artifacts = ROOT/'shared/build/ui-test-artifacts'
artifacts.mkdir(parents=True, exist_ok=True)
reference = None
for runtime in runtimes(args.runtime):
    capture, state = artifacts/(runtime+'.bmp'), artifacts/(runtime+'.json')
    result = launch(runtime, '--frames', 3, '--capture', capture, '--dump-state', state)
    unavailable = any(message in result.stderr for message in (
        'The video driver did not add any displays', 'No available video device',
        'No supported SDL_GPU backend found', 'No available GPU device'))
    if result.returncode and unavailable and not args.require_gpu:
        print(runtime + ': SKIP UI GPU (no display/backend)')
        continue
    passed(result)
    data = json.loads(state.read_text())
    assert data['frames'] == 3 and data['primitives'] == 16, data
    assert data['remaining_resources'] == [0, 0, 0, 0], data
    width, height, rows = runner.bmp_pixels(capture)
    assert (width, height) == (640, 480)
    assert pixel(rows, 0, 0) == (16, 24, 39)
    assert pixel(rows, 220, 150) == (112, 156, 255)
    assert pixel(rows, 24, 24) == (16, 24, 39), 'Rounded corner should reveal background'
    if reference:
        assert rows == reference[0], 'C/Chez captures differ on the same GPU'
        assert data['nodes'] == reference[1], 'C/Chez bounds differ'
    reference = rows, data['nodes']
    scaled = artifacts/(runtime+'-scaled.bmp')
    passed(launch(runtime, '--width', 480, '--height', 320, '--scale', 2, '--capture', scaled))
    sw, sh, scaled_rows = runner.bmp_pixels(scaled)
    assert (sw, sh) == (960, 640)
    assert pixel(scaled_rows, 100, 100) == (103, 217, 192), 'Logical-to-physical scale differs'
    probe = artifacts/(runtime+'-probe.bmp')
    passed(launch(runtime, '--scene', 'ui.paint', '--width', 160,
                  '--height', 160, '--capture', probe))
    _, _, rows = runner.bmp_pixels(probe)
    assert pixel(rows, 150, 70) == (16, 24, 39), 'Widget paint must be clipped'
    assert pixel(rows, 30, 70) == (255, 0, 0), 'Base rectangle missing'
    overlap = pixel(rows, 80, 70)
    assert abs(overlap[0]-127) <= 1 and overlap[1] == 0 and abs(overlap[2]-128) <= 1, overlap
    assert pixel(rows, 36, 36) == (0, 255, 0)
    assert pixel(rows, 20, 20) == (255, 0, 0), 'Rounded green corner should reveal red'
    assert any(0 < pixel(rows, x, y)[0] < 255 and 0 < pixel(rows, x, y)[1] < 255
               for y in range(20, 32) for x in range(20, 32)), 'Rounded edge should be antialiased'
    interaction = artifacts/(runtime+'-interaction.bmp')
    scripted = launch(runtime, '--scene', 'ui.interaction',
                      '--script', 'ui.interaction-script',
                      '--frames', 3, '--width', 160, '--height', 160, '--capture', interaction)
    passed(scripted)
    assert 'UI-RELOAD retained' in scripted.stdout, scripted.stdout
    assert pixel(runner.bmp_pixels(interaction)[2], 50, 50) == (0, 255, 0)
    clicked = artifacts/(runtime+'-clicked.bmp')
    passed(launch(runtime, '--script', 'ui.pointer-script', '--frames', 3, '--capture', clicked))
    assert pixel(runner.bmp_pixels(clicked)[2], 220, 150) == (103, 217, 192)
    quit_state = artifacts/(runtime+'-quit.json')
    quit_capture = artifacts/(runtime+'-quit.png')
    quitting = launch(runtime, '--script', 'ui.quit-script', '--frames', 10,
                         '--dump-state', quit_state,
                         '--capture', quit_capture)
    passed(quitting)
    assert json.loads(quit_state.read_text())['frames'] == 3
    assert quit_capture.read_bytes().startswith(b'\x89PNG\r\n\x1a\n')
    failed = launch(runtime, '--inject-error')
    assert failed.returncode != 0 and 'injected UI failure' in failed.stderr
    assert 'UI-CLEANUP 0 0 0 0' in failed.stdout, failed.stdout
    failed = launch(runtime, '--driver', 'otium-invalid-driver')
    assert failed.returncode != 0 and 'UI-CLEANUP 0 0 0 0' in failed.stdout
    print(runtime + ': GPU capture, scaling, clipping, blending, scripted input/reload and error cleanup passed')
