"""Check dispatch and exact argv forwarding without requiring either runtime."""
import json
import os
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest

from otium_tools import ROOT, command, environment


class LauncherTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory(prefix='otium launcher ')
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        shutil.copy2(ROOT / 'otium', self.root / 'otium')
        for runtime, path in [('c', 'c/otium'), ('chez', 'chez/run')]:
            stub = self.root / path
            stub.parent.mkdir()
            stub.write_text('#!/usr/bin/env python3\nimport json,sys\n'
                            f'print(json.dumps([{runtime!r}, sys.argv[1:]]))\n')
            stub.chmod(0o755)

    def run_launcher(self, *args, status=0):
        result = subprocess.run([str(self.root / 'otium'), *args], text=True,
                                capture_output=True, cwd='/', timeout=10)
        self.assertEqual(result.returncode, status, result.stderr)
        return json.loads(result.stdout) if status == 0 else result.stderr

    def test_default_and_both_selector_forms(self):
        self.assertEqual(self.run_launcher(), ['chez', []])
        self.assertEqual(self.run_launcher('--runtime=c', 'x.ot'), ['c', ['x.ot']])
        self.assertEqual(self.run_launcher('--runtime=chez', 'x.ot'), ['chez', ['x.ot']])
        self.assertEqual(self.run_launcher('x.ot', '--runtime', 'chez'), ['chez', ['x.ot']])

    def test_arguments_are_preserved(self):
        args = ['--path', 'with spaces', 'program.ot', '--', '', 'λ', '--runtime=c', '--', '--help']
        self.assertEqual(self.run_launcher('--runtime=chez', *args), ['chez', args])

    def test_formatter_and_linter_run_on_chez(self):
        lib, tools = str(self.root / 'shared/lib'), str(self.root / 'shared/tools')
        for tool in ('fmt', 'lint'):
            self.assertEqual(self.run_launcher(tool, 'a.ot', '--check'),
                             ['chez', ['--no-project', '--path', lib, f'{tools}/{tool}.ot', '--', 'a.ot', '--check']])

    def test_option_values_are_not_selectors(self):
        for option in ('--path', '-L', '--project', '--load'):
            args = [option, '--runtime=chez', 'x.ot']
            self.assertEqual(self.run_launcher(*args), ['chez', args])

    def test_invalid_selector(self):
        for args in [('--runtime',), ('--runtime=',), ('--runtime=bad',), ('--runtime', 'bad')]:
            self.run_launcher(*args, status=2)

    def test_explicit_runtime_and_environment(self):
        self.assertEqual(command('chez', 'file.ot')[1:], ['--runtime=chez', '--no-project', 'file.ot'])
        self.assertEqual(command('c', executable='/tmp/custom')[0], '/tmp/custom')
        previous = os.environ.get('OTIUM_FFI_LIBRARY')
        try:
            os.environ['OTIUM_FFI_LIBRARY'] = '/tmp/custom library'
            self.assertEqual(environment()['OTIUM_FFI_LIBRARY'], '/tmp/custom library')
        finally:
            if previous is None:
                os.environ.pop('OTIUM_FFI_LIBRARY', None)
            else:
                os.environ['OTIUM_FFI_LIBRARY'] = previous
