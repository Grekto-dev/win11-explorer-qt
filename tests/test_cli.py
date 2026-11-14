#!/usr/bin/env python3
"""Runtime safety boundary checks; use a separate D-Bus session.

SPDX-License-Identifier: GPL-2.0-or-later
"""
import os
from pathlib import Path
import signal
import subprocess
import tempfile
import time
import unittest

BINARY = Path(os.environ.get('EXPLORER_BINARY', Path(__file__).resolve().parents[1]/'build/pcmanfm/win11-explorer-qt'))


class CommandLineSafety(unittest.TestCase):
    def run_cli(self, arguments):
        with tempfile.TemporaryDirectory(prefix='explorer-cli-') as config:
            env = dict(os.environ, QT_QPA_PLATFORM='offscreen', XDG_CONFIG_HOME=config,
                       XDG_CACHE_HOME=str(Path(config)/'cache'), XDG_DATA_HOME=str(Path(config)/'data'))
            return subprocess.run([str(BINARY), *arguments], env=env, text=True,
                                  capture_output=True, timeout=10, check=False)

    def test_desktop_takeover_options_return_failure(self):
        for arguments in (['--desktop'], ['--desktop-off'], ['--desktop-pref','general'],
                          ['--set-wallpaper','/nonexistent'], ['--wallpaper-mode','fit'], ['--daemon-mode']):
            with self.subTest(arguments=arguments):
                result = self.run_cli(arguments)
                self.assertEqual(result.returncode,1,result.stderr)
                self.assertIn('Unsupported option',result.stderr)

    def test_unsafe_profile_names_return_failure(self):
        for profile in ('../outside','/absolute','a/b','..'):
            with self.subTest(profile=profile):
                result = self.run_cli(['--profile',profile])
                self.assertEqual(result.returncode,1,result.stderr)
                self.assertIn('Invalid profile name',result.stderr)


class RunningInstance(unittest.TestCase):
    """Second-instance and no-bus behaviour (run under dbus-run-session)."""
    SERVICE = 'org.grekto.Win11ExplorerQt'

    def make_env(self, home, **extra):
        return dict(os.environ, QT_QPA_PLATFORM='offscreen', HOME=home, XDG_CONFIG_HOME=home + '/cfg',
                    XDG_CACHE_HOME=home + '/cache', XDG_DATA_HOME=home + '/data', **extra)

    def start_primary(self, home):
        env = self.make_env(home)
        primary = subprocess.Popen([str(BINARY), '-n', home], env=env, text=True,
                                   stdout=subprocess.DEVNULL, stderr=subprocess.PIPE)
        self.addCleanup(self.stop, primary)
        deadline = time.time() + 15
        while time.time() < deadline:
            probe = subprocess.run(['gdbus', 'introspect', '--session', '--dest', self.SERVICE,
                                    '--object-path', '/Application'], capture_output=True, env=env)
            if probe.returncode == 0:
                return env
            time.sleep(0.2)
        self.fail('primary instance never registered on D-Bus')

    @staticmethod
    def stop(proc):
        if proc.poll() is None:
            proc.send_signal(signal.SIGTERM)
            try:
                proc.wait(5)
            except subprocess.TimeoutExpired:
                proc.kill()
        if proc.stderr:
            proc.stderr.close()

    def second(self, env, arguments, timeout=8):
        started = time.time()
        try:
            result = subprocess.run([str(BINARY), *arguments], env=env, text=True, capture_output=True, timeout=timeout)
        except subprocess.TimeoutExpired:
            self.fail('second instance blocked for %ss with %r' % (timeout, arguments))
        return result, time.time() - started

    def test_second_instance_missing_folder_fails_fast_with_message(self):
        with tempfile.TemporaryDirectory(prefix='explorer-p4-') as home:
            env = self.start_primary(home)
            result, elapsed = self.second(env, [home + '/does-not-exist-p4'])
            self.assertLess(elapsed, 5, result.stderr)
            self.assertNotEqual(result.returncode, 0, result.stderr)
            self.assertIn('does-not-exist-p4', result.stderr)

    def test_tilde_is_expanded_on_the_command_line(self):
        with tempfile.TemporaryDirectory(prefix='explorer-p4-') as home:
            env = self.start_primary(home)
            result, _ = self.second(env, ['~/nope-p4'])
            self.assertNotEqual(result.returncode, 0, result.stderr)
            self.assertIn(home + '/nope-p4', result.stderr)
            result, elapsed = self.second(env, ['~'])
            self.assertEqual(result.returncode, 0, result.stderr)
            self.assertLess(elapsed, 5)

    def test_without_session_bus_it_never_exits_silently(self):
        with tempfile.TemporaryDirectory(prefix='explorer-p4-') as home:
            env = self.make_env(home, DBUS_SESSION_BUS_ADDRESS='unix:path=' + home + '/no-bus')
            proc = subprocess.Popen([str(BINARY), '-n', home], env=env, text=True,
                                    stdout=subprocess.DEVNULL, stderr=subprocess.PIPE)
            self.addCleanup(self.stop, proc)
            time.sleep(3)
            if proc.poll() is None:
                return  # works without single-instance support
            err = proc.stderr.read()
            self.assertNotEqual(proc.returncode, 0, 'silent rc=0 exit without a bus')
            self.assertTrue(err.strip(), 'no explanation on stderr')


if __name__ == '__main__':
    unittest.main(verbosity=2)
