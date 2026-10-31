#!/usr/bin/env python3
"""Runtime safety boundary checks; use a separate D-Bus session.

SPDX-License-Identifier: GPL-2.0-or-later
"""
import os
from pathlib import Path
import subprocess
import tempfile
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


if __name__ == '__main__':
    unittest.main(verbosity=2)
