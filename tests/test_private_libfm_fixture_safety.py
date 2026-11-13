#!/usr/bin/env python3
"""Exercise cache fixture ownership using only task-owned sandbox inputs."""
import hashlib
import os
from pathlib import Path
import subprocess
import tempfile
import unittest
from unittest import mock

import test_private_libfm_cmake as fixture
import test_private_libfm_guards as guards
from private_libfm_fixture import fixture_plan, preflight

ROOT = Path(__file__).resolve().parents[1]


class FixtureSafety(unittest.TestCase):
    def test_input_at_legacy_copy_destination_survives(self):
        with tempfile.TemporaryDirectory(prefix='fixture-safety-', dir=ROOT / '.state') as tmp:
            root = Path(tmp)
            overlay = root / '.state/cache-test-prefix'
            overlay.mkdir(parents=True)
            marker = overlay / 'task-owned-marker'
            marker.write_bytes(b'fixture input must survive unchanged\n')
            before = hashlib.sha256(marker.read_bytes()).hexdigest()
            stop = RuntimeError('stop before real configure')
            with mock.patch.object(fixture, 'ROOT', root), \
                    mock.patch.dict(os.environ, {'L4_TEST_PREFIX': str(overlay)}), \
                    mock.patch.object(fixture.subprocess, 'run', side_effect=stop):
                try:
                    fixture.PrivateLibfm('test_stale_cache_then_prefix_switch').test_stale_cache_then_prefix_switch()
                except Exception as exc:
                    failure = exc
                else:
                    self.fail('expected configure boundary')
            self.assertTrue(marker.exists(), 'cache fixture deleted its input overlay')
            self.assertEqual(hashlib.sha256(marker.read_bytes()).hexdigest(), before)
            self.assertIs(failure, stop)
            self.assertEqual(sorted(p.name for p in overlay.iterdir()), ['task-owned-marker'])
            self.assertFalse(list((root / '.state').glob('private-libfm-*')),
                             'task-owned temporary workspace was not cleaned')


    def test_persistent_logs_never_modify_selected_input(self):
        for module, logname in ((fixture, 'authorized-build'), (guards, 'build-safety-logs')):
            for alias in (False, True):
                with self.subTest(module=module.__name__, alias=alias), tempfile.TemporaryDirectory(
                        prefix='fixture-log-input-', dir=ROOT / '.state') as tmp:
                    root = Path(tmp)
                    overlay = root / '.state' / logname
                    overlay.mkdir(parents=True)
                    (overlay / 'marker').write_bytes(b'protected input')
                    if alias:
                        target = root / 'input'
                        overlay.rename(target)
                        overlay.symlink_to(target, target_is_directory=True)
                    before = sorted((str(p.relative_to(overlay)), p.read_bytes())
                                    for p in overlay.rglob('*') if p.is_file())
                    (root / 'cmake').mkdir()
                    (root / 'cmake/PrivateLibfm.cmake').write_text(
                        (ROOT / 'cmake/PrivateLibfm.cmake').read_text())
                    stop = RuntimeError('configure boundary') if module is fixture else None
                    with mock.patch.object(module, 'ROOT', root), mock.patch.dict(
                            os.environ, {'L4_TEST_PREFIX': str(overlay)}), mock.patch.object(
                            module.subprocess, 'run', side_effect=stop,
                            return_value=subprocess.CompletedProcess([], 0, 'fixture output', '')):
                        try:
                            if module is fixture:
                                module.PrivateLibfm().test_stale_cache_then_prefix_switch()
                            else:
                                module.PrivatePackageGuards().configure('valid')
                        except (ValueError, RuntimeError):
                            pass
                    after = sorted((str(p.relative_to(overlay)), p.read_bytes())
                                   for p in overlay.rglob('*') if p.is_file())
                    self.assertEqual(before, after)
                    self.assertEqual(sorted(p.name for p in overlay.iterdir()), ['marker'])
                    if alias:
                        self.assertTrue(overlay.is_symlink())
                    self.assertFalse(list((root / '.state').glob('private-libfm-*')))

    def test_physical_overlap_and_aliases_are_rejected(self):
        with tempfile.TemporaryDirectory(prefix='fixture-paths-', dir=ROOT / '.state') as tmp:
            root = Path(tmp)
            overlay = root / 'overlay'
            overlay.mkdir()
            marker = overlay / 'marker'
            marker.write_bytes(b'owned sentinel')
            alias = root / 'alias'
            alias.symlink_to(overlay, target_is_directory=True)
            for output in (overlay, root, overlay / 'child', alias, alias / 'child'):
                with self.subTest(output=output), self.assertRaises(ValueError):
                    fixture.require_disjoint(output, overlay)
            fixture.require_disjoint(root / 'sibling', overlay)
            with mock.patch.object(fixture, 'ROOT', overlay), self.assertRaises(ValueError):
                with fixture.fixture_workspace(overlay):
                    self.fail('allocated a workspace inside the input')
            self.assertEqual(marker.read_bytes(), b'owned sentinel')
            self.assertEqual(list(overlay.iterdir()), [marker])

    def test_sdk_and_source_aliases_are_readonly_including_allocation(self):
        with tempfile.TemporaryDirectory(prefix='fixture-readonly-', dir=ROOT / '.state') as tmp:
            root = Path(tmp)
            sdk = root / '.deps'
            source = root / 'sources'
            for directory in (sdk, source):
                directory.mkdir()
                (directory / 'marker').write_bytes(b'readonly')
            overlay = root / 'overlay'
            overlay.mkdir()
            for protected in (sdk, source):
                state = root / '.state'
                state.symlink_to(protected, target_is_directory=True)
                before = sorted(p.name for p in protected.iterdir())
                with self.subTest(protected=protected), self.assertRaises(ValueError):
                    with fixture_plan(root, overlay, state / 'logs'):
                        self.fail('allocated inside readonly input')
                self.assertEqual(sorted(p.name for p in protected.iterdir()), before)
                state.unlink()
            (root / '.state').mkdir()
            for protected in (sdk, source):
                alias = root / '.state/log-alias'
                alias.symlink_to(protected, target_is_directory=True)
                with fixture_plan(root, overlay, alias) as (workspace, logs):
                    self.assertFalse(logs.is_relative_to(protected))
                    (logs / 'result').write_text('owned output')
                    self.assertTrue(workspace.is_dir())
                self.assertFalse(workspace.exists())
                self.assertEqual(sorted(p.name for p in protected.iterdir()), ['marker'])
                self.assertTrue(alias.is_symlink())
                alias.unlink()

    def test_complete_output_plan_rejects_physical_collisions(self):
        with tempfile.TemporaryDirectory(prefix='fixture-plan-', dir=ROOT / '.state') as tmp:
            root = Path(tmp)
            first = root / 'output'
            first.mkdir()
            alias = root / 'alias'
            alias.symlink_to(first, target_is_directory=True)
            for other in (first, first / 'child', root, alias, alias / 'child'):
                with self.subTest(other=other), self.assertRaises(ValueError):
                    preflight([first, other], [root / 'input'])

    def test_ctest_consumes_configured_custom_prefix_without_shell_override(self):
        with tempfile.TemporaryDirectory(prefix='fixture-ctest-', dir=ROOT / '.state') as tmp:
            root = Path(tmp)
            prefix = root / '.state/only-custom-prefix'
            prefix.mkdir(parents=True)
            (prefix / 'marker').write_text('configured overlay consumed')
            (root / 'tests').mkdir()
            (root / 'tests/test_private_libfm_cmake.py').write_text('''import os
from pathlib import Path
prefix = Path(os.environ.get('L4_TEST_PREFIX', '.state/libfm-overlay-prefix'))
assert (prefix / 'marker').read_text() == 'configured overlay consumed'
print('CONSUMED=' + str(prefix))
''')
            source = (ROOT / 'CMakeLists.txt').read_text()
            start = source.index('    add_test(NAME private-libfm-cache-selection')
            end = source.index('    set_tests_properties(overlay-path-safety', start)
            (root / 'CMakeLists.txt').write_text('''cmake_minimum_required(VERSION 3.18)
project(ctest-prefix-fixture NONE)
include(CTest)
set(Python3_EXECUTABLE /usr/bin/python3)
get_filename_component(_private_fm_prefix "${LIBFMQT_OVERLAY_PREFIX}" REALPATH)
''' + source[start:end])
            env = dict(os.environ)
            env.pop('L4_TEST_PREFIX', None)
            for command in (
                ['uv', 'tool', 'run', '--from', 'cmake', 'cmake', '-S', str(root),
                 '-B', str(root / 'build'), '-DLIBFMQT_OVERLAY_PREFIX=' + str(prefix)],
                ['uv', 'tool', 'run', '--from', 'cmake', 'ctest', '--test-dir',
                 str(root / 'build'), '-V'],
            ):
                result = subprocess.run(command, env=env, text=True, capture_output=True)
                self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
            self.assertIn('CONSUMED=' + str(prefix), result.stdout)
            self.assertFalse((root / '.state/libfm-overlay-prefix').exists())


if __name__ == '__main__':
    unittest.main()
