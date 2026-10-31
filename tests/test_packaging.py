# SPDX-License-Identifier: GPL-2.0-or-later
import configparser
import os
from pathlib import Path
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
INSTALLER = ROOT / 'scripts/install-dev.sh'


class DesktopEntryTest(unittest.TestCase):
    def test_has_separate_identity_and_no_system_claims(self):
        path = ROOT / 'packaging/win11-explorer-qt.desktop'
        self.assertTrue(path.is_file(), 'Fork needs its own desktop entry')
        config = configparser.ConfigParser(interpolation=None)
        config.read(path)
        entry = config['Desktop Entry']
        self.assertEqual(entry['Exec'], 'win11-explorer-qt %U')
        self.assertEqual(entry['StartupWMClass'], 'win11-explorer-qt')
        self.assertNotIn('MimeType', entry)
        self.assertNotIn('DBusActivatable', entry)
        self.assertNotIn('X-GNOME-Autostart-enabled', entry)
        self.assertEqual(entry['Terminal'], 'false')


class DevInstallerTest(unittest.TestCase):
    def run_installer(self, prefix):
        return subprocess.run(
            [str(INSTALLER), '--prefix', str(prefix)],
            cwd=ROOT, text=True, capture_output=True,
        )

    def test_installs_only_user_scoped_launcher_desktop_and_icon(self):
        with tempfile.TemporaryDirectory() as tmp:
            home = Path(tmp) / 'home'
            home.mkdir()
            result = self.run_installer(home)
            self.assertEqual(result.returncode, 0, result.stderr)
            self.assertEqual(os.readlink(home / '.local/bin/win11-explorer-qt'), str(ROOT / 'scripts/run.sh'))
            self.assertTrue((home / '.local/share/applications/win11-explorer-qt.desktop').is_file())
            icon = home / '.local/share/icons/hicolor/scalable/apps/win11-explorer-qt.svg'
            desktop = home / '.local/share/applications/win11-explorer-qt.desktop'
            self.assertTrue(icon.is_file())
            self.assertTrue(icon.is_symlink(), 'Development icon must follow the original fork artwork live')
            self.assertTrue(desktop.is_symlink(), 'Development desktop entry must follow this checkout')
            self.assertEqual(icon.resolve(), ROOT / 'icons/explorer/scalable/places/folder.svg')
            relative = {str(p.relative_to(home)) for p in home.rglob('*')}
            self.assertEqual(relative, {
                '.local', '.local/bin', '.local/bin/win11-explorer-qt',
                '.local/share', '.local/share/applications',
                '.local/share/applications/win11-explorer-qt.desktop',
                '.local/share/icons', '.local/share/icons/hicolor',
                '.local/share/icons/hicolor/scalable',
                '.local/share/icons/hicolor/scalable/apps',
                '.local/share/icons/hicolor/scalable/apps/win11-explorer-qt.svg',
            })

    def test_prefix_can_contain_spaces_and_colons(self):
        with tempfile.TemporaryDirectory() as tmp:
            home = Path(tmp) / 'isolated: home'
            result = self.run_installer(home)
            self.assertEqual(result.returncode, 0, result.stderr)
            self.assertTrue((home / '.local/bin/win11-explorer-qt').is_symlink())

    def test_conflicting_desktop_file_does_not_partially_install(self):
        with tempfile.TemporaryDirectory() as tmp:
            home = Path(tmp) / 'home'
            desktop = home / '.local/share/applications/win11-explorer-qt.desktop'
            desktop.parent.mkdir(parents=True)
            desktop.write_text('preserve unrelated file')
            result = self.run_installer(home)
            self.assertNotEqual(result.returncode, 0)
            self.assertEqual(desktop.read_text(), 'preserve unrelated file')
            self.assertFalse((home / '.local/bin/win11-explorer-qt').exists())

    def test_same_target_install_is_idempotent(self):
        with tempfile.TemporaryDirectory() as tmp:
            home = Path(tmp) / 'home'
            home.mkdir()
            first = self.run_installer(home)
            second = self.run_installer(home)
            self.assertEqual(first.returncode, 0, first.stderr)
            self.assertEqual(second.returncode, 0, second.stderr)
            self.assertEqual(os.readlink(home / '.local/bin/win11-explorer-qt'), str(ROOT / 'scripts/run.sh'))

    def test_refuses_to_replace_unrelated_launcher_symlink(self):
        with tempfile.TemporaryDirectory() as tmp:
            home = Path(tmp) / 'home'
            target = Path(tmp) / 'unrelated-launcher'
            home.mkdir()
            target.write_text('keep me')
            launcher = home / '.local/bin/win11-explorer-qt'
            launcher.parent.mkdir(parents=True)
            launcher.symlink_to(target)
            result = self.run_installer(home)
            self.assertNotEqual(result.returncode, 0)
            self.assertEqual(os.readlink(launcher), str(target))
            self.assertFalse((home / '.local/share/applications/win11-explorer-qt.desktop').exists())


if __name__ == '__main__':
    unittest.main(verbosity=2)
