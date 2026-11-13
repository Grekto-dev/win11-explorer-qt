#!/usr/bin/python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Fail-closed wrapper tests; no desktop or injected input."""
import fcntl
import os
from pathlib import Path
import subprocess
import tempfile
import unittest
from unittest import mock

import run_selection_x11 as runner

ROOT = Path(__file__).resolve().parents[1]

class SelectionWrapperSafety(unittest.TestCase):
    def run_wrapper(self, target, scratch, extra=None):
        env = dict(os.environ, TMPDIR=str(scratch), EXPLORER_XTEST_DISPLAY=target,
                   EXPLORER_TEST_ARTIFACTS=str(scratch / 'artifacts'))
        env.update(extra or {})
        return subprocess.run(['bash', str(ROOT / 'tests/run_selection_xtest.sh'), '/usr/bin/true'],
                              env=env, capture_output=True, text=True, timeout=5)

    def test_session_rejects_different_target_without_open_or_input(self):
        self.check_session_target(':19')

    def test_cleanup_rechecks_target_without_open_or_input(self):
        self.check_session_target(':18', change_at_cleanup=True)

    def check_session_target(self, target, change_at_cleanup=False):
        # Isolate all process/X11 side effects. The ownership seam accepts only
        # :18; neither a real personal server nor a WM is ever contacted.
        from Xlib import display
        from Xlib.ext import xtest
        with tempfile.TemporaryDirectory(dir=os.environ['TMPDIR']) as tmp:
            env = dict(os.environ, DISPLAY=':18', EXPLORER_XTEST_DISPLAY=':18')
            def verify(environment):
                if environment['DISPLAY'] != ':18':
                    raise RuntimeError('ownership changed')
            def execute(*args, **kwargs):
                if change_at_cleanup:
                    kwargs['env']['EXPLORER_XTEST_DISPLAY'] = ':19'
                return subprocess.CompletedProcess(args, 0)
            argv = ['runner', '--session', '--display', target,
                    '--binary', '/usr/bin/true', '--artifacts', tmp, '--case', 'probe']
            with mock.patch.dict(os.environ, env, clear=True), \
                 mock.patch('sys.argv', argv), \
                 mock.patch.object(runner, 'verify_environment', side_effect=verify), \
                 mock.patch.object(runner.time, 'sleep'), \
                 mock.patch.object(runner.subprocess, 'Popen') as start, \
                 mock.patch.object(runner.subprocess, 'run', side_effect=execute) as run, \
                 mock.patch.object(display, 'Display') as opened, \
                 mock.patch.object(xtest, 'fake_input') as injected:
                start.return_value.poll.return_value = 0
                code = 0
                try:
                    code = runner.main()
                except SystemExit as error:
                    code = error.code
                self.assertEqual(opened.call_count, 0, 'unverified X display opened')
                self.assertEqual(injected.call_count, 0, 'input sent without target proof')
                self.assertEqual(code, 2)
                if not change_at_cleanup:
                    start.assert_not_called()
                    run.assert_not_called()

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
