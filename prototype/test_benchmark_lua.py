#!/usr/bin/env python3
"""Check reference-runner failures and rejection of samples containing compilation."""
import pathlib
import subprocess
import tempfile

from benchmark_lua import LUA_CASES, available_library, run_lua

ROOT = pathlib.Path(__file__).resolve().parent.parent


def main():
    library = available_library()
    if not library:
        print('LuaJIT benchmark tests skipped: shared library unavailable')
        return
    runner = str(ROOT / 'prototype/bench_lua')
    assert subprocess.run([runner], capture_output=True).returncode == 2
    assert subprocess.run([runner, '/missing/luajit.so', '/missing/script.lua'],
                          capture_output=True).returncode == 1
    with tempfile.TemporaryDirectory(prefix='otium-lua-test-') as directory:
        directory = pathlib.Path(directory)
        script = directory / 'error.lua'
        for source in ['local =', 'error("expected")', 'error({})']:
            script.write_text(source)
            result = subprocess.run([runner, library, str(script)], capture_output=True, text=True)
            assert result.returncode == 1 and result.stderr
        LUA_CASES['warmup-test'] = '''local calls = 0
        local function kernel(n)
          calls = calls + 1
          if calls >= 4 then
            local total = 0
            for i = 1, n do total = total + i end
            return total + 42 - total
          end
          return 42
        end'''
        measured = run_lua(ROOT, directory, library, 'warmup-test', 10000, 42, True)
        assert measured['warmups'] >= 4
        assert measured['discarded_samples'] >= 1
        assert measured['trace_starts'] > 0
        LUA_CASES['wrong-answer'] = 'local function kernel(n) return 0 end'
        try:
            run_lua(ROOT, directory, library, 'wrong-answer', 1, 42, False)
        except RuntimeError as error:
            assert 'wrong benchmark result' in str(error)
        else:
            raise AssertionError('wrong result was accepted')
    print('LuaJIT benchmark error and warmup tests passed')


if __name__ == '__main__':
    main()
