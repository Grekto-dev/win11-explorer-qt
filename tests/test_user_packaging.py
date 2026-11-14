#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Packaging contract: optimized overlay, launcher, desktop entry, user installer.
Runs only in task-owned temporary directories; never touches the real HOME."""
import os
import subprocess
import tempfile
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
INSTALL = ROOT / 'scripts/install-user.sh'
CLEAN_ENV = {'PATH': '/usr/bin:/bin'}


def fake_prefix(base: Path) -> Path:
    prefix = base / 'opt dir ção' / 'win11-explorer-qt'
    (prefix / 'bin').mkdir(parents=True)
    (prefix / 'bin/win11-explorer-qt').write_text('#!/bin/sh\necho fake\n')
    (prefix / 'bin/win11-explorer-qt').chmod(0o755)
    (prefix / 'share/icons/hicolor/scalable/apps').mkdir(parents=True)
    (prefix / 'share/icons/hicolor/scalable/apps/win11-explorer-qt.svg').write_bytes(
        (ROOT / 'packaging/icons/win11-explorer-qt.svg').read_bytes())
    (prefix / 'share/win11-explorer-qt').mkdir(parents=True)
    (prefix / 'share/win11-explorer-qt/win11-explorer-qt.desktop.in').write_text(
        (ROOT / 'packaging/win11-explorer-qt.desktop.in').read_text())
    return prefix


def run_install(home: Path, prefix: Path, *args):
    env = dict(CLEAN_ENV, HOME=str(home))
    return subprocess.run(['bash', str(INSTALL), '--prefix', str(prefix), *args],
                          env=env, text=True, capture_output=True)


class OverlayBuildType(unittest.TestCase):
    def test_overlay_script_honours_requested_build_type(self):
        text = (ROOT / 'scripts/fidelity/build-libfm-overlay.sh').read_text()
        self.assertIn('-DCMAKE_BUILD_TYPE="${LIBFMQT_OVERLAY_BUILD_TYPE:-Debug}"', text)
        self.assertNotIn('-DCMAKE_BUILD_TYPE=Debug', text)

    def test_build_script_passes_its_build_type_to_overlay(self):
        text = (ROOT / 'scripts/build.sh').read_text()
        self.assertIn('export LIBFMQT_OVERLAY_BUILD_TYPE="$build_type"', text)


class DesktopEntry(unittest.TestCase):
    def setUp(self):
        self.text = (ROOT / 'packaging/win11-explorer-qt.desktop.in').read_text()

    def test_offers_folders_in_open_with_without_becoming_default(self):
        self.assertIn('MimeType=inode/directory;', self.text)
        self.assertIn('[Desktop Action NewWindow]', self.text)
        self.assertIn('Actions=NewWindow;', self.text)

    def test_exec_placeholder_is_replaced_by_installer_not_left_relative(self):
        self.assertIn('Exec=@BINDIR@/win11-explorer-qt %U', self.text)
        self.assertIn('Icon=win11-explorer-qt', self.text)


class UserInstaller(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.addCleanup(self.tmp.cleanup)
        self.base = Path(self.tmp.name)
        self.home = self.base / 'home'
        self.home.mkdir()
        self.prefix = fake_prefix(self.base)
        self.desktop = self.home / '.local/share/applications/win11-explorer-qt.desktop'
        self.launcher = self.home / '.local/bin/win11-explorer-qt'
        self.icon = self.home / '.local/share/icons/hicolor/scalable/apps/win11-explorer-qt.svg'

    def test_dry_run_writes_nothing(self):
        result = run_install(self.home, self.prefix, '--dry-run')
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertFalse((self.home / '.local').exists())
        self.assertIn(str(self.desktop), result.stdout)

    def test_install_creates_real_desktop_with_absolute_exec_and_no_default_registration(self):
        result = run_install(self.home, self.prefix)
        self.assertEqual(result.returncode, 0, result.stderr)
        text = self.desktop.read_text()
        self.assertFalse(self.desktop.is_symlink())
        self.assertIn(f'Exec={self.prefix}/bin/win11-explorer-qt %U', text)
        self.assertNotIn('@BINDIR@', text)
        self.assertTrue(self.launcher.is_symlink())
        self.assertEqual(os.readlink(self.launcher), str(self.prefix / 'bin/win11-explorer-qt'))
        self.assertTrue(self.icon.is_file() and not self.icon.is_symlink())
        self.assertFalse((self.home / '.config').exists(), 'must not create mimeapps/config')
        self.assertFalse((self.home / '.local/share/applications/mimeinfo.cache').exists())

    def test_install_is_idempotent(self):
        self.assertEqual(run_install(self.home, self.prefix).returncode, 0)
        before = self.desktop.read_text()
        again = run_install(self.home, self.prefix)
        self.assertEqual(again.returncode, 0, again.stderr)
        self.assertEqual(self.desktop.read_text(), before)

    def test_refuses_to_overwrite_foreign_files(self):
        self.launcher.parent.mkdir(parents=True)
        self.launcher.write_text('someone else')
        result = run_install(self.home, self.prefix)
        self.assertNotEqual(result.returncode, 0)
        self.assertEqual(self.launcher.read_text(), 'someone else')
        self.assertFalse(self.desktop.exists(), 'validate everything before writing')

    def test_uninstall_removes_only_own_files(self):
        self.assertEqual(run_install(self.home, self.prefix).returncode, 0)
        other = self.home / '.local/share/applications/other.desktop'
        other.write_text('keep')
        result = run_install(self.home, self.prefix, '--uninstall')
        self.assertEqual(result.returncode, 0, result.stderr)
        for path in (self.desktop, self.icon):
            self.assertFalse(path.exists())
        self.assertFalse(os.path.lexists(self.launcher))
        self.assertEqual(other.read_text(), 'keep')

    def test_uninstall_leaves_foreign_launcher_alone(self):
        self.launcher.parent.mkdir(parents=True)
        self.launcher.write_text('someone else')
        result = run_install(self.home, self.prefix, '--uninstall')
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(self.launcher.read_text(), 'someone else')

    def test_rejects_prefix_without_binary(self):
        (self.prefix / 'bin/win11-explorer-qt').unlink()
        result = run_install(self.home, self.prefix)
        self.assertNotEqual(result.returncode, 0)
        self.assertFalse((self.home / '.local').exists())


class Launcher(unittest.TestCase):
    def test_wrapper_template_is_hermetic(self):
        text = (ROOT / 'packaging/win11-explorer-qt.sh').read_text()
        for forbidden in ('uv ', '.deps', 'env.sh', '.state', 'export LD_LIBRARY_PATH',
                          'export QT_PLUGIN_PATH', 'check-private-libfm-runtime'):
            self.assertNotIn(forbidden, text)
        self.assertIn('-style Fusion', text)
        self.assertNotIn('QT_STYLE_OVERRIDE', text.replace('unset QT_STYLE_OVERRIDE', ''))


class PortablePrefixGuards(unittest.TestCase):
    """Refusals happen before any write, without needing a build."""
    def make(self, *args, home=None):
        env = dict(CLEAN_ENV, HOME=str(home or tempfile.gettempdir()))
        return subprocess.run(['bash', str(ROOT / 'scripts/make-portable-prefix.sh'), *args],
                              env=env, text=True, capture_output=True)

    def test_refuses_existing_directory_without_marker(self):
        with tempfile.TemporaryDirectory() as tmp:
            target = Path(tmp) / 'a' / 'precious'
            target.mkdir(parents=True)
            (target / 'file').write_text('keep')
            install, overlay = Path(tmp) / 'install', Path(tmp) / 'overlay'
            (install / 'bin').mkdir(parents=True)
            (install / 'bin/win11-explorer-qt').write_text('x')
            (overlay / 'lib').mkdir(parents=True)
            (overlay / 'lib/libfm-qt6.so.17').write_text('x')
            result = self.make('--prefix', str(target), '--install-dir', str(install), '--overlay', str(overlay))
            self.assertIn('not a prefix created by this script', result.stderr)
            self.assertNotEqual(result.returncode, 0)
            self.assertEqual((target / 'file').read_text(), 'keep')

    def test_refuses_prefix_inside_dev_tree(self):
        for inside in ('.state/x/prefix', '.deps/x/prefix'):
            result = self.make('--prefix', str(ROOT / inside))
            self.assertNotEqual(result.returncode, 0, inside)
            self.assertIn('may not live under', result.stderr)
            self.assertFalse((ROOT / inside).exists(), inside)

    def test_refuses_home_and_relative_prefix(self):
        with tempfile.TemporaryDirectory() as tmp:
            self.assertNotEqual(self.make('--prefix', tmp, home=tmp).returncode, 0)
            self.assertNotEqual(self.make('--prefix', 'relative/dir', home=tmp).returncode, 0)


class DebPackage(unittest.TestCase):
    def test_deb_has_system_paths_explicit_depends_and_no_dev_tree(self):
        with tempfile.TemporaryDirectory() as tmp:
            prefix = fake_prefix(Path(tmp))
            out = Path(tmp) / 'out'
            result = subprocess.run(['bash', str(ROOT / 'scripts/make-deb.sh'), '--prefix', str(prefix),
                                     '--out', str(out)], env=dict(CLEAN_ENV, HOME=tmp), text=True, capture_output=True)
            self.assertEqual(result.returncode, 0, result.stderr)
            debs = list(out.glob('win11-explorer-qt_*_amd64.deb'))
            self.assertEqual(len(debs), 1, debs)
            info = subprocess.run(['dpkg-deb', '-I', str(debs[0])], text=True, capture_output=True).stdout
            self.assertIn('Package: win11-explorer-qt', info)
            self.assertIn('libglib2.0-0t64 (>= 2.80)', info)
            self.assertIn('libmenu-cache3', info)
            listing = subprocess.run(['dpkg-deb', '-c', str(debs[0])], text=True, capture_output=True).stdout
            self.assertIn('./opt/win11-explorer-qt/bin/win11-explorer-qt', listing)
            self.assertIn('./usr/share/applications/win11-explorer-qt.desktop', listing)
            self.assertIn('./usr/bin/win11-explorer-qt -> /opt/win11-explorer-qt/bin/win11-explorer-qt', listing)
            self.assertNotIn('.deps', listing)
            self.assertNotIn('./etc/', listing)
            desktop = subprocess.run(['dpkg-deb', '--fsys-tarfile', str(debs[0])], capture_output=True).stdout
            tar = subprocess.run(['tar', '-xO', './usr/share/applications/win11-explorer-qt.desktop'],
                                 input=desktop, capture_output=True).stdout.decode()
            self.assertIn('Exec=/opt/win11-explorer-qt/bin/win11-explorer-qt %U', tar)
            self.assertNotIn('@BINDIR@', tar)
            self.assertFalse(any(m in listing for m in ('mimeapps', 'autostart')))


if __name__ == '__main__':
    unittest.main()
