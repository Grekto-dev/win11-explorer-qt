#!/usr/bin/env python3
"""Destructive regression fixtures never reference the real SDK as a destination."""
import os
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
SDK = ROOT / '.deps/src/libfm-qt'

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

    def test_all_destinations_reject_symlinks_escapes_and_overlap(self):
        import runpy
        validate = runpy.run_path(str(ROOT / 'scripts/fidelity/overlay-preflight.py'))['validate']
        with tempfile.TemporaryDirectory(prefix='guard-', dir=ROOT / '.state') as tmp:
            root = Path(tmp)
            source = root / 'sdk'
            source.mkdir()
            sentinel = source / 'sentinel'
            sentinel.write_text('intact')
            state = root / '.state'
            state.mkdir()
            targets = [state / 'overlay-source/libfm-qt', state / 'libfm-overlay-build', state / 'prefix', state / 'test-build']
            self.assertEqual(validate(root, source, targets), targets)
            for index, target in enumerate(targets):
                target.parent.mkdir(exist_ok=True)
                for link in (target, target.parent) if target.parent != state else (target,):
                    with self.subTest(destination=index, symlink=str(link)):
                        if link.exists():
                            link.rmdir()
                        link.symlink_to(source, target_is_directory=True)
                        with self.assertRaises(ValueError):
                            validate(root, source, targets)
                        link.unlink()
                        self.assertEqual(sentinel.read_text(), 'intact')
            for bad in (root / 'outside', state, state / 'overlay-source', targets[0]):
                with self.subTest(overlap=str(bad)), self.assertRaises(ValueError):
                    validate(root, source, [*targets, bad])
            inside_source = state / 'sdk'
            inside_source.mkdir()
            with self.assertRaisesRegex(ValueError, 'overlaps SDK'):
                validate(root, inside_source, [inside_source / 'child'])
            with self.assertRaisesRegex(ValueError, 'overlaps SDK'):
                validate(root, inside_source / '.', [inside_source])
            # An entirely redirected .state is also rejected.
            shutil.rmtree(state)
            state.symlink_to(source, target_is_directory=True)
            with self.assertRaisesRegex(ValueError, 'symlink'):
                validate(root, source, targets)
            self.assertEqual(sentinel.read_text(), 'intact')

    def test_archive_does_not_copy_hidden_or_dirty_source(self):
        with tempfile.TemporaryDirectory(prefix='archive-', dir=ROOT / '.state') as tmp:
            root = Path(tmp)
            (root / 'scripts/fidelity').mkdir(parents=True)
            for name in ('build-libfm-overlay.sh', 'overlay-preflight.py'):
                shutil.copy2(ROOT / 'scripts/fidelity' / name, root / 'scripts/fidelity')
            if os.environ.get('OVERLAY_ARCHIVE_MUTATION'):
                script = root / 'scripts/fidelity/build-libfm-overlay.sh'
                script.write_text(script.read_text().replace('git -C "$SDK_SOURCE" archive --format=tar "$EXPECTED_REV" | tar -xf - -C "$OVERLAY_SOURCE"', 'cp -a "$SDK_SOURCE/." "$OVERLAY_SOURCE/"'))
            (root / '.deps/src').mkdir(parents=True)
            subprocess.run(['git', '-c', 'advice.detachedHead=false', 'clone', '--quiet', '--shared', str(SDK), str(root / '.deps/src/libfm-qt')], check=True)
            source = root / '.deps/src/libfm-qt'
            (source / '.hidden-untracked').write_text('must not copy')
            original = (source / 'AUTHORS').read_bytes()
            (source / 'AUTHORS').write_bytes(original + b'\nDIRTY SOURCE\n')
            (root / '.deps/env.sh').write_text('NINJA_BIN=/bin/true\nSDK_PREFIX=/fixture\nQT_ROOT=/fixture\nSYSROOT=/fixture\ncmake() { echo EXPECTED_CONFIGURE_STOP >&2; return 91; }\n')
            shutil.copytree(ROOT / 'patches', root / 'patches')
            result = subprocess.run(['bash', str(root / 'scripts/fidelity/build-libfm-overlay.sh')], text=True, capture_output=True)
            self.assertEqual(result.returncode, 91, result.stderr)
            self.assertIn('EXPECTED_CONFIGURE_STOP', result.stderr)
            archive = root / '.state/overlay-source/libfm-qt'
            self.assertFalse((archive / '.hidden-untracked').exists())
            self.assertFalse((archive / '.git').exists())
            self.assertEqual((archive / 'AUTHORS').read_bytes(), original)
            expected_license = subprocess.check_output(['git', '-C', str(source), 'show', '4294f29ed314388f1f6d051ae0128320354e417a:LICENSE'])
            self.assertEqual((archive / 'LICENSE').read_bytes(), expected_license)

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
