#!/usr/bin/env python3
"""Repair a failing or looping live UI through its development socket."""
from pathlib import Path
import os
import subprocess
import tempfile
import time
import unittest
from shared.tests.test_dev import Client, ROOT, RUNTIMES


class LiveUI(unittest.TestCase):
    def test_repair(self):
        for runtime in RUNTIMES:
            with self.subTest(runtime=runtime), tempfile.TemporaryDirectory(prefix='otdev-ui-', dir='/tmp') as folder:
                folder = Path(folder)
                path = folder / 'dev.sock'
                script = folder / 'ui.ot'
                script.write_text('''(require (otium.ui :as ui))
(def (view) (ui/label {} "Initial"))
(defonce window (ui/open {:headless #t :width 160 :height 100}))
(ui/set-view! window (fn () (view)))
(ui/run)
''')
                p = subprocess.Popen([str(ROOT/'otium'), '--runtime='+runtime, '--no-project',
                                      '-L', str(ROOT/'shared/lib'), '--dev='+str(path),
                                      '--dev-watchdog=500', str(script)], cwd=ROOT,
                                     stdout=subprocess.PIPE, stderr=subprocess.PIPE)
                client = None
                try:
                    for _ in range(400):
                        if path.exists():
                            break
                        if p.poll() is not None:
                            self.fail(str(p.communicate()))
                        time.sleep(.025)
                    client = Client(path)
                    client.send(1, 'eval', code='(length (ui/windows))')
                    self.assertIn(':value "1"', client.response(1))
                    client.send(2, 'eval', code='(def (view) (error "frame broke"))')
                    client.response(2)
                    while True:
                        event = client.read()
                        if ':event :frame-error' in event:
                            self.assertIn('frame broke', event)
                            break
                    client.send(3, 'eval', code='(def (view) (ui/label {} "Repaired"))')
                    self.assertIn(":ok #t", client.response(3))
                    capture = folder/'fixed.png'
                    client.send(4, 'eval', code='(ui/capture! window "%s")' % capture)
                    self.assertIn(":ok #t", client.response(4))
                    for _ in range(200):
                        if capture.exists():
                            break
                        time.sleep(.025)
                    self.assertTrue(capture.exists())
                    self.assertEqual(capture.read_bytes()[:8], b'\x89PNG\r\n\x1a\n')
                    client.send(5, 'eval', code='(def (view) (let loop () (loop)))')
                    client.response(5)
                    while ':event :paused' not in client.read():
                        pass
                    client.send(6, 'eval', code='(def (view) (ui/label {} "Resumed"))')
                    client.response(6)
                    client.send(7, 'abort')
                    client.response(7)
                    client.send(8, 'eval', code='(length (ui/windows))')
                    self.assertIn(':value "1"', client.response(8))
                    client.send(9, 'eval', code='(ui/close! window)')
                    client.response(9)
                    out, err = p.communicate(timeout=5)
                    self.assertEqual(p.returncode, 0, (out, err))
                finally:
                    if client:
                        client.close()
                    if p.poll() is not None:
                        out, err = p.communicate()
                        if err: print(runtime, out.decode(), err.decode())
                    if p.poll() is None:
                        p.terminate()
                        try:
                            out, err = p.communicate(timeout=3)
                            if err: print(runtime, out.decode(), err.decode())
                        except subprocess.TimeoutExpired:
                            p.kill()
                            p.communicate()


if __name__ == '__main__':
    unittest.main()
