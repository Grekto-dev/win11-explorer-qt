#!/usr/bin/python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Selection harness must reject unsafe/occupied configurable displays."""
import os
from pathlib import Path
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]

class SelectionWrapperSafety(unittest.TestCase):
    def run_wrapper(self, target, occupied=False):
        with tempfile.TemporaryDirectory(dir=ROOT / '.state') as tmp:
            root = Path(tmp)
            probe = root / 'xdpyinfo'
            probe.write_text('#!/bin/sh\n[ "$DISPLAY" = "$OCCUPIED_DISPLAY" ]\n')
            probe.chmod(0o755)
            env = dict(os.environ, PATH=f'{root}:/usr/bin:/bin',
                       EXPLORER_XTEST_DISPLAY=target,
                       OCCUPIED_DISPLAY=target if occupied else ':999',
                       EXPLORER_TEST_ARTIFACTS=str(root / 'artifacts'))
            return subprocess.run(['bash', str(ROOT / 'tests/run_selection_xtest.sh'), '/usr/bin/true'],
                                  env=env, capture_output=True, text=True, timeout=5)

    def test_refuses_desktop_displays_before_starting_xephyr(self):
        for target in (':0', ':0.0', ':1', ':1.0', 'localhost:0', ''):
            with self.subTest(target=target):
                result = self.run_wrapper(target)
                self.assertEqual(result.returncode, 2)
                self.assertIn('Unsafe isolated display', result.stderr)

    def test_refuses_preexisting_configured_display(self):
        result = self.run_wrapper(':17', occupied=True)
        self.assertEqual(result.returncode, 2)
        self.assertIn('pre-existing display :17', result.stderr)

if __name__ == '__main__':
    unittest.main(verbosity=2)
