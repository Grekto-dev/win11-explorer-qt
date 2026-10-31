#!/usr/bin/python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Fail-closed wrapper tests; no desktop or injected input."""
import fcntl
import os
from pathlib import Path
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]

class SelectionWrapperSafety(unittest.TestCase):
    def run_wrapper(self, target, scratch, extra=None):
        env = dict(os.environ, TMPDIR=str(scratch), EXPLORER_XTEST_DISPLAY=target,
                   EXPLORER_TEST_ARTIFACTS=str(scratch / 'artifacts'))
        env.update(extra or {})
        return subprocess.run(['bash', str(ROOT / 'tests/run_selection_xtest.sh'), '/usr/bin/true'],
                              env=env, capture_output=True, text=True, timeout=5)

    def test_refuses_desktop_displays_before_starting_xephyr(self):
        with tempfile.TemporaryDirectory(dir=os.environ['TMPDIR']) as tmp:
            for target in (':0', ':0.0', ':1', ':1.0', 'localhost:0', ''):
                with self.subTest(target=target):
                    result = self.run_wrapper(target, Path(tmp))
                    self.assertEqual(result.returncode, 2)
                    self.assertIn('Unsafe isolated display', result.stderr)

    def test_exclusive_common_scratch_lock_precedes_server_start(self):
        with tempfile.TemporaryDirectory(dir=os.environ['TMPDIR']) as tmp:
            root = Path(tmp)
            with (root / 'explorer-x11-display-18.lock').open('a') as lock:
                fcntl.flock(lock, fcntl.LOCK_EX | fcntl.LOCK_NB)
                result = self.run_wrapper(':18', root)
                self.assertEqual(result.returncode, 2)
                self.assertIn('reserved', result.stderr)
                self.assertFalse((root / 'artifacts/xephyr.log').exists())

    def test_direct_binary_requires_real_server_ownership_before_qapplication(self):
        binary = Path(os.environ.get('EXPLORER_TEST_BINARY', ROOT / '.state/build/tests/tst_explorer'))
        if not binary.is_file():
            self.fail('Build tst_explorer before running ownership regression')
        env = dict(os.environ, DISPLAY=':18', EXPLORER_XTEST_DISPLAY=':18',
                   EXPLORER_ENABLE_XTEST='1', QT_QPA_PLATFORM='offscreen')
        for name in ('EXPLORER_XSERVER_PID', 'EXPLORER_XSERVER_START',
                     'EXPLORER_XOWNER_PID', 'EXPLORER_XOWNER_START'):
            env.pop(name, None)
        result = subprocess.run([str(binary), 'defaultSettingsMatchExplorerDensity'],
                                env=env, capture_output=True, text=True, timeout=5)
        self.assertEqual(result.returncode, 2)
        self.assertIn('XTEST ownership rejected', result.stderr)
        self.assertNotIn('Start testing', result.stdout)

    def test_dead_own_server_never_accepts_another_server_readiness(self):
        with tempfile.TemporaryDirectory(dir=os.environ['TMPDIR']) as tmp:
            root = Path(tmp)
            fake = root / 'Xephyr'
            fake.write_text('#!/bin/sh\nexit 7\n')
            fake.chmod(0o755)
            result = self.run_wrapper(':918', root, {'PATH': str(root) + ':/usr/bin:/bin'})
            self.assertNotEqual(result.returncode, 0)
            self.assertIn('Xephyr exited', result.stderr)
            self.assertFalse((root / 'artifacts/wm.log').exists())

if __name__ == '__main__':
    unittest.main(verbosity=2)
