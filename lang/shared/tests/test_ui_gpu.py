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
text_reference = None
calculator_reference = None
terminal_reference = None
editor_reference = None
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
    assert data['frames'] == 3 and data['primitives'] > 150, data
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
    for density in (1, 2):
        resized = artifacts/f'{runtime}-resize-{density}.bmp'
        resized_state = artifacts/f'{runtime}-resize-{density}.json'
        result = launch(runtime, '--scene', 'ui.resize', '--script', 'ui.resize',
                        '--frames', 4, '--scale', density, '--capture', resized,
                        '--dump-state', resized_state)
        passed(result)
        assert 'UI-RESIZE layout, input and identity passed' in result.stdout
        assert runner.bmp_pixels(resized)[:2] == (480*density, 300*density)
        resize_data = json.loads(resized_state.read_text())
        assert (resize_data['width'], resize_data['height']) == (480, 300)
        assert resize_data['remaining_resources'] == [0, 0, 0, 0]
    direct_state = artifacts/(runtime+'-direct.json')
    direct = launch(runtime, '--scene', 'ui.resize', '--script', 'ui.resize',
                    '--background', '--dump-state', direct_state, headless=False)
    passed(direct)
    assert 'UI-RENDER-MODE direct' in direct.stdout, direct.stdout
    assert 'UI-RESIZE layout, input and identity passed' in direct.stdout
    assert json.loads(direct_state.read_text())['width'] == 480
    for headless in (True, False):
        contract = launch(runtime, '--script', 'ui.renderer-contract', '--background', headless=headless)
        passed(contract)
        assert 'UI-RENDERER target contract passed' in contract.stdout
        mode = 'offscreen' if headless else 'direct'
        assert 'UI-RENDER-MODE ' + mode in contract.stdout
    _, _, direct_rows = runner.bmp_pixels(artifacts/'direct-capture.bmp')
    assert pixel(direct_rows, 0, 0) == (103, 217, 192)
    lw, lh, large_rows = runner.bmp_pixels(artifacts/'large-target.bmp')
    assert (lw, lh) == (5000, 32)
    assert pixel(large_rows, 4999, 31) == (103, 217, 192)
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
    text_capture = artifacts/(runtime+'-text.bmp')
    passed(launch(runtime, '--scene', 'ui.text', '--width', 160, '--height', 120,
                  '--frames', 3, '--capture', text_capture))
    _, _, text_rows = runner.bmp_pixels(text_capture)
    assert any(pixel(text_rows, x, y)[0] > 180
               for y in range(10, 60) for x in range(10, 130)), 'Text is missing'
    assert any(0 < pixel(text_rows, x, y)[0] < 180
               for y in range(10, 60) for x in range(10, 130)), 'Glyphs lack grayscale AA'
    assert all(pixel(text_rows, x, y) == (16, 24, 39)
               for y in range(10, 90) for x in range(150, 160)), 'Text escaped its clip'
    assert any(0 < pixel(text_rows, x, y)[0] <= 128 and pixel(text_rows, x, y)[1] == 0
               for y in range(65, 88) for x in range(90, 140)), 'Text alpha/color missing'
    if text_reference is not None:
        assert text_rows == text_reference, 'C/Chez shaped glyph captures differ'
    text_reference = text_rows
    target = platform.machine() + '-' + platform.system().lower()
    oracle = ROOT/'shared/build'/target/'ui/otium-ui-text-reference'
    for density in (1, 2):
        expected = artifacts/f'text-reference-{density}.bmp'
        subprocess.run([str(oracle), str(density), str(expected)], check=True, cwd=ROOT)
        actual = artifacts/f'{runtime}-text-{density}x.bmp'
        passed(launch(runtime, '--scene', 'ui.text', '--width', 160, '--height', 120,
                      '--scale', density, '--capture', actual))
        expected_rows = runner.bmp_pixels(expected)[2]
        actual_rows = runner.bmp_pixels(actual)[2]
        # Exclude the colored overlay at the bottom of the probe.
        for y in range(10*density, 60*density):
            assert actual_rows[y][30*density:450*density] == expected_rows[y][30*density:450*density], (
                runtime, density, y, 'Atlas text differs from SDL_ttf surface rendering')
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
    calculator = artifacts/(runtime+'-calculator.bmp')
    calculator_state = artifacts/(runtime+'-calculator.json')
    result = launch(runtime, '--scene', 'ui.calculator', '--script', 'ui.calculator-script',
                    '--frames', 4, '--height', 600, '--capture', calculator,
                    '--dump-state', calculator_state)
    passed(result)
    assert 'UI-CALCULATOR native keys and widgets passed' in result.stdout
    calculator_rows = runner.bmp_pixels(calculator)[2]
    if calculator_reference is not None:
        assert calculator_rows == calculator_reference, 'C/Chez calculator captures differ'
    calculator_reference = calculator_rows
    nodes = json.loads(calculator_state.read_text())['nodes']
    focused = [n for n in nodes if n['focused']]
    assert len(focused) == 1 and focused[0]['pressed'], focused
    assert any(n['disabled'] for n in nodes), 'Disabled composed widget missing'
    button = focused[0]
    x, y = round(button['x']), round(button['y'] + button['height']/2)
    assert pixel(calculator_rows, x+1, y) == (168, 213, 255), 'Focus outline missing'
    assert pixel(calculator_rows, x+5, y) == (66, 89, 118), 'Pressed fill missing'
    terminal = artifacts/(runtime+'-terminal.bmp')
    terminal_state = artifacts/(runtime+'-terminal.json')
    result = launch(runtime, '--scene', 'ui.terminal', '--script', 'ui.terminal-script',
                    '--frames', 5, '--capture', terminal, '--dump-state', terminal_state)
    passed(result)
    assert 'UI-TERMINAL native text, history, wheel and selection passed' in result.stdout
    terminal_rows = runner.bmp_pixels(terminal)[2]
    if terminal_reference is not None:
        assert terminal_rows == terminal_reference, 'C/Chez terminal captures differ'
    terminal_reference = terminal_rows
    field = next(n for n in json.loads(terminal_state.read_text())['nodes'] if n['focused'])
    x, y = round(field['x']), round(field['y'])
    assert pixel(terminal_rows, x+15, y+9) == (66, 89, 118), 'Selection background missing'
    assert any(pixel(terminal_rows, cx, y+9) == (103, 217, 192)
               for cx in range(x+10, x+100)), 'Text caret missing'
    assert all(pixel(terminal_rows, 10, cy) == (16, 24, 39)
               for cy in range(60, 360)), 'Scrollback escaped viewport'
    for cursor_frames in (3, 5):
        cursor_capture = artifacts/f'{runtime}-terminal-cursor-{cursor_frames}.bmp'
        cursor_state = artifacts/f'{runtime}-terminal-cursor-{cursor_frames}.json'
        result = launch(runtime, '--scene', 'ui.terminal', '--script', 'ui.terminal-cursor',
                        '--frames', cursor_frames, '--capture', cursor_capture, '--dump-state', cursor_state)
        passed(result)
        field = next(n for n in json.loads(cursor_state.read_text())['nodes'] if n['focused'])
        rows = runner.bmp_pixels(cursor_capture)[2]
        y = round(field['y']) + 9
        green = [x for x in range(round(field['x']) + 10, round(field['x'] + field['width']) - 10)
                 if pixel(rows, x, y) == (103, 217, 192)]
        assert len(green) >= 8 and green[-1] - green[0] + 1 == len(green), 'Block cursor is missing or clipped'
        if cursor_frames == 3:
            assert green[0] >= round(field['x']) + 40, 'Cursor did not advance over trailing spaces'
        else:
            assert 'UI-TERMINAL block cursor and spaces passed' in result.stdout
    contract = launch(runtime, '--script', 'ui.event-contract', '--title', 'Custom test title')
    passed(contract)
    assert 'UI-EVENT copied named events passed' in contract.stdout
    passed(launch(runtime, '--scene', 'ui.terminal', '--script', 'ui.terminal-script',
                  '--frames', 1, '--title', 'Short terminal test'))
    policy = launch(runtime, '--scene', 'ui.policy-scene', '--script', 'ui.policy-scene', '--frames', 4)
    passed(policy)
    assert 'UI-POLICY custom native text policy passed' in policy.stdout, policy.stdout
    editor = artifacts/(runtime+'-editor.bmp')
    result = launch(runtime, '--scene', 'ui.editor', '--script', 'ui.editor', '--frames', 4,
                    '--width', 300, '--height', 160, '--capture', editor)
    passed(result)
    assert 'UI-EDITOR horizontal scrolling and pointer selection passed' in result.stdout
    editor_rows = runner.bmp_pixels(editor)[2]
    if editor_reference is not None:
        assert editor_rows == editor_reference, 'C/Chez editor captures differ'
    editor_reference = editor_rows
    assert all(pixel(editor_rows, 250, cy) == (16, 24, 39)
               for cy in range(50, 110)), 'Long input escaped its clip'
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
