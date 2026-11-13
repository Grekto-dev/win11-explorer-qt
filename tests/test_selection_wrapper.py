#!/usr/bin/python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Fail-closed wrapper tests; no desktop or injected input."""
from contextlib import redirect_stderr
import fcntl
import io
import os
from pathlib import Path
import subprocess
import tempfile
import unittest
from unittest import mock

import run_selection_x11 as runner

ROOT = Path(__file__).resolve().parents[1]

class SelectionWrapperSafety(unittest.TestCase):
    def setUp(self):
        # Explicit local base: CTest must not depend on Hermes or tempfile's
        # platform fallback (/tmp), even when TMPDIR/TEMP/TMP are absent.
        base = ROOT / '.state/selection-wrapper-tests'
        base.mkdir(parents=True, exist_ok=True)
        temporary = tempfile.TemporaryDirectory(dir=base)
        self.addCleanup(temporary.cleanup)
        self.scratch = Path(temporary.name)

    def run_wrapper(self, target, scratch, extra=None):
        env = dict(os.environ, TMPDIR=str(scratch), EXPLORER_XTEST_DISPLAY=target,
                   EXPLORER_TEST_ARTIFACTS=str(scratch / 'artifacts'))
        env.update(extra or {})
        return subprocess.run(['bash', str(ROOT / 'tests/run_selection_xtest.sh'), '/usr/bin/true'],
                              env=env, capture_output=True, text=True, timeout=5)

    def test_session_rejects_different_target_without_open_or_input(self):
        self.check_session_target(':19')

    def test_session_rejects_different_target_when_owned_target_is_19(self):
        self.check_session_target(':18', owned_target=':19')

    def test_cleanup_rechecks_target_without_open_or_input(self):
        self.check_session_target(':18', change_at_cleanup=True)

    def check_session_target(self, target, change_at_cleanup=False, owned_target=':18'):
        # Isolate all process/X11 side effects; no server is ever contacted.
        from Xlib import display
        from Xlib.ext import xtest
        with tempfile.TemporaryDirectory(dir=self.scratch) as tmp:
            env = dict(os.environ, DISPLAY=owned_target, EXPLORER_XTEST_DISPLAY=owned_target)
            def verify(environment):
                if environment['DISPLAY'] != owned_target:
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
        with tempfile.TemporaryDirectory(dir=self.scratch) as tmp:
            for target in (':0', ':0.0', ':1', ':1.0', 'localhost:0', ''):
                with self.subTest(target=target):
                    result = self.run_wrapper(target, Path(tmp))
                    self.assertEqual(result.returncode, 2)
                    self.assertIn('Unsafe isolated display', result.stderr)

    def test_exclusive_common_scratch_lock_precedes_server_start(self):
        with tempfile.TemporaryDirectory(dir=self.scratch) as tmp:
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
                   EXPLORER_ENABLE_XTEST='1', QT_QPA_PLATFORM='xcb')
        for name in ('EXPLORER_XSERVER_PID', 'EXPLORER_XSERVER_START',
                     'EXPLORER_XOWNER_PID', 'EXPLORER_XOWNER_START'):
            env.pop(name, None)
        result = subprocess.run([str(binary), 'defaultSettingsMatchExplorerDensity'],
                                env=env, capture_output=True, text=True, timeout=5)
        self.assertEqual(result.returncode, 2)
        self.assertIn('XTEST ownership rejected', result.stderr)
        self.assertNotIn('Start testing', result.stdout)

    def test_server_startup_failure_matrix(self):
        # Real harmless children, no fake X server or personal display. Control
        # the scheduling boundary explicitly instead of racing an exit script.
        from Xlib import display
        from Xlib.ext import xtest
        real_popen = subprocess.Popen
        real_identity = runner.process_identity
        for tmpdir in (True, False):
            for boundary in ('before-zombie', 'before-reaped', 'after', 'alive-invalid'):
                with self.subTest(tmpdir=tmpdir, boundary=boundary), \
                     tempfile.TemporaryDirectory(dir=self.scratch) as tmp:
                    root = Path(tmp)
                    artifacts = root / 'artifacts'
                    env = dict(os.environ, TEMP=str(root), TMP=str(root))
                    env.pop('TMPDIR', None)
                    if tmpdir:
                        env['TMPDIR'] = str(root)
                    children = []
                    def start(command, **kwargs):
                        self.assertEqual(command[0], 'Xephyr', 'WM started before proof')
                        source = ('raise SystemExit(7)' if boundary.startswith('before')
                                  else 'import signal; signal.pause()')
                        child = real_popen(['/usr/bin/python3', '-c', source], **kwargs)
                        children.append(child)
                        if boundary == 'before-zombie':
                            os.waitid(os.P_PID, child.pid, os.WEXITED | os.WNOWAIT)
                        elif boundary == 'before-reaped':
                            child.wait(timeout=3)
                        return child
                    def identity(pid):
                        if children and pid == children[0].pid:
                            if boundary == 'alive-invalid':
                                raise RuntimeError('Live identity sentinel')
                            value = real_identity(pid)
                            if boundary == 'after':
                                children[0].terminate()
                                children[0].wait(timeout=3)
                            return value
                        return real_identity(pid)
                    argv = ['runner', '--display', ':918', '--binary', '/usr/bin/true',
                            '--artifacts', str(artifacts)]
                    stderr = io.StringIO()
                    # Observe the selected base while retaining the real lock,
                    # filesystem setup, process identity and stop/cleanup.
                    real_reserve = runner.reserve
                    reservations = []
                    def reserve(target, scratch):
                        reservations.append(scratch)
                        return real_reserve(target, scratch)
                    try:
                        with mock.patch.dict(os.environ, env, clear=True), \
                             mock.patch('sys.argv', argv), redirect_stderr(stderr), \
                             mock.patch.object(runner, 'reserve', side_effect=reserve), \
                             mock.patch.object(runner.subprocess, 'Popen', side_effect=start) as spawned, \
                             mock.patch.object(runner, 'process_identity', side_effect=identity), \
                             mock.patch.object(runner.subprocess, 'run') as session, \
                             mock.patch.object(display, 'Display') as opened, \
                             mock.patch.object(xtest, 'fake_input') as injected:
                            with self.assertRaises(SystemExit) as raised:
                                runner.main()
                            self.assertEqual(raised.exception.code, 2)
                            spawned.assert_called_once()
                            session.assert_not_called()
                            opened.assert_not_called()
                            injected.assert_not_called()
                        expected_scratch = (root if tmpdir else
                                            ROOT / '.state/selection-x11-scratch')
                        self.assertEqual(reservations, [expected_scratch.resolve()])
                        self.assertIsNotNone(children[0].poll(), 'child leaked')
                        expected_status = 7 if boundary.startswith('before') else -15
                        self.assertEqual(children[0].returncode, expected_status)
                        self.assertFalse(Path(f'/proc/{children[0].pid}').exists())
                        diagnostic = stderr.getvalue()
                        if boundary == 'alive-invalid':
                            self.assertIn('Live identity sentinel', diagnostic)
                            self.assertNotIn('Xephyr exited', diagnostic)
                        else:
                            self.assertIn('Xephyr exited before opening :918', diagnostic)
                        self.assertTrue((artifacts / 'xephyr.log').is_file())
                        for name in ('config', 'cache', 'data', 'state', 'runtime'):
                            self.assertTrue((artifacts / ('xdg-' + name)).is_dir())
                        self.assertTrue((artifacts / 'tmp').is_dir())
                        self.assertFalse((artifacts / 'wm.log').exists())
                        self.assertFalse((artifacts / 'ownership.json').exists())
                        self.assertFalse((artifacts / 'runner-result.json').exists())
                        with real_reserve(':918', expected_scratch):
                            pass  # Same lock can be reacquired after every failure.
                    finally:
                        for child in children:
                            runner.stop(child)
                self.assertFalse(root.exists(), 'per-case directory leaked')

    def test_dead_own_server_never_accepts_another_server_readiness(self):
        with tempfile.TemporaryDirectory(dir=self.scratch) as tmp:
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
