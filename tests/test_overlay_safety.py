#!/usr/bin/env python3
"""Destructive regression fixtures never reference the real SDK as a destination."""
import os
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
SDK = Path('/home/pedro/win11-explorer-qt/.deps/src/libfm-qt')

class OverlaySafety(unittest.TestCase):
    def test_state_itself_is_never_a_destination(self):
        import runpy
        validate = runpy.run_path(str(ROOT / 'scripts/fidelity/overlay-preflight.py'))['validate']
        with tempfile.TemporaryDirectory(prefix='guard-', dir=ROOT / '.state') as tmp:
            root = Path(tmp)
            (root / 'sdk').mkdir()
            with self.assertRaisesRegex(ValueError, 'state root'):
                validate(root, root / 'sdk', [root / '.state'])

    def test_rejects_traversal_before_normalizing(self):
        import runpy
        validate = runpy.run_path(str(ROOT / 'scripts/fidelity/overlay-preflight.py'))['validate']
        with tempfile.TemporaryDirectory(prefix='guard-', dir=ROOT / '.state') as tmp:
            root = Path(tmp)
            (root / 'sdk').mkdir()
            (root / '.state').mkdir()
            (root / '.state/escape').symlink_to(root / 'sdk')
            with self.assertRaises(ValueError):
                validate(root, root / 'sdk', [root / '.state/escape/../destination'])

    def test_intermediate_symlink_preserves_sandbox_sdk(self):
        state = ROOT / '.state'
        state.mkdir(exist_ok=True)
        with tempfile.TemporaryDirectory(prefix='safety-', dir=state) as tmp:
            root = Path(tmp)
            (root / 'scripts/fidelity').mkdir(parents=True)
            for script in (ROOT / 'scripts/fidelity').glob('*.py'):
                shutil.copy2(script, root / 'scripts/fidelity')
            shutil.copy2(ROOT / 'scripts/fidelity/build-libfm-overlay.sh', root / 'scripts/fidelity')
            (root / '.deps/src').mkdir(parents=True)
            subprocess.run(['git', 'clone', '--quiet', '--shared', str(SDK), str(root / '.deps/src/libfm-qt')], check=True)
            source = root / '.deps/src/libfm-qt'
            sentinel = source / 'TASK_OWNED_SENTINEL'
            sentinel.write_text('preserve me\n')
            self.assertTrue((source / 'LICENSE').is_file())
            self.assertEqual(subprocess.check_output(['git', '-C', str(source), 'rev-parse', 'HEAD'], text=True).strip(), '4294f29ed314388f1f6d051ae0128320354e417a')
            (root / '.deps/env.sh').write_text('cmake() { echo UNEXPECTED_BUILD >&2; return 91; }\n')
            (root / '.state').mkdir()
            (root / '.state/overlay-source').symlink_to(root / '.deps/src', target_is_directory=True)
            result = subprocess.run(['bash', str(root / 'scripts/fidelity/build-libfm-overlay.sh')], text=True, capture_output=True)
            self.assertTrue(sentinel.exists(), result.stderr)
            self.assertIn('Unsafe overlay path', result.stderr)
            self.assertNotIn('Missing local', result.stderr)
            self.assertFalse((root / '.state/libfm-overlay-prefix').exists())

if __name__ == '__main__':
    unittest.main()
