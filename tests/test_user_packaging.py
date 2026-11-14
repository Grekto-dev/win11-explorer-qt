#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Packaging contract: optimized overlay, launcher, desktop entry, user installer.
Runs only in task-owned temporary directories; never touches the real HOME."""
import importlib.util
import os
import re
import shutil
import subprocess
import tempfile
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
INSTALL = ROOT / 'scripts/install-user.sh'
CLEAN_ENV = {'PATH': '/usr/bin:/bin'}


def load(name):
    spec = importlib.util.spec_from_file_location(name, ROOT / 'scripts' / f'{name}.py')
    mod = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(mod)
    return mod


def add_docs(prefix: Path):
    """Give a fake prefix the license documents the real assembler installs."""
    dest = prefix / 'share/doc/win11-explorer-qt'
    dest.mkdir(parents=True, exist_ok=True)
    pkg = load('pkg_licenses')
    for name in pkg.REQUIRED_DOCS:
        (dest / name).write_text(f'text of {name}\n')


def fake_prefix(base: Path) -> Path:
    prefix = base / 'opt dir ção' / 'win11-explorer-qt'
    (prefix / 'bin').mkdir(parents=True)
    (prefix / 'bin/win11-explorer-qt').write_text('#!/bin/sh\necho fake\n')
    (prefix / 'bin/win11-explorer-qt').chmod(0o755)
    (prefix / '.win11-explorer-qt-prefix').write_text('created by scripts/make-portable-prefix.sh\n')
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


def exec_argv(desktop_text: str, action=None):
    """Decode the Exec= key following the Desktop Entry spec (string escapes, then quoting, then %%)."""
    section = None
    for line in desktop_text.splitlines():
        if line.startswith('['):
            section = line.strip('[]')
        want = 'Desktop Entry' if action is None else f'Desktop Action {action}'
        if section == want and line.startswith('Exec='):
            raw = line[len('Exec='):]
            break
    else:
        raise AssertionError('no Exec')
    raw = re.sub(r'\\(.)', lambda m: {'s': ' ', 'n': '\n', 't': '\t', 'r': '\r'}.get(m.group(1), m.group(1)), raw)
    args, cur, quoted, i, have = [], '', False, 0, False
    while i < len(raw):
        c = raw[i]
        if quoted and c == '\\':
            cur += raw[i + 1]; i += 2; continue
        if c == '"':
            quoted = not quoted; have = True
        elif c == ' ' and not quoted:
            if have or cur:
                args.append(cur); cur, have = '', False
        else:
            cur += c; have = True
        i += 1
    if have or cur:
        args.append(cur)
    return [a.replace('%%', '%') if '%%' in a else a for a in args]


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
        self.assertEqual(exec_argv(text), [f'{self.prefix}/bin/win11-explorer-qt', '%U'])
        self.assertNotIn('@BINDIR@', text)
        self.assertTrue(self.launcher.is_symlink())
        self.assertEqual(os.readlink(self.launcher), str(self.prefix / 'bin/win11-explorer-qt'))
        self.assertTrue(self.icon.is_file() and not self.icon.is_symlink())
        self.assertFalse((self.home / '.config').exists(), 'must not create mimeapps/config')
        self.assertFalse((self.home / '.local/share/applications/mimeapps.list').exists())
        self.assertFalse((self.home / '.local/share/applications/defaults.list').exists())

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
        self.assertNotIn('QT_STYLE_OVERRIDE', text)
        self.assertNotIn('LD_LIBRARY_PATH', text.replace('# ', ''), 'wrapper must not touch the loader path')


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


class DesktopQuoting(unittest.TestCase):
    NASTY = ['plain', 'with space', 'amp&ersand', 'pct%U %f', 'quo"te', 'dollar$HOME', 'back`tick`', 'back\\slash', 'ção é']

    def test_exec_roundtrips_for_hostile_prefixes(self):
        mod = load('render_desktop')
        template = (ROOT / 'packaging/win11-explorer-qt.desktop.in').read_text()
        for name in self.NASTY:
            bindir = f'/opt/{name}/bin'
            text = mod.render(template, bindir)
            self.assertEqual(exec_argv(text), [f'{bindir}/win11-explorer-qt', '%U'], name)
            self.assertEqual(exec_argv(text, 'NewWindow'), [f'{bindir}/win11-explorer-qt', '-n'], name)
            self.assertNotIn('@BINDIR@', text)

    def test_newline_or_control_characters_are_rejected(self):
        mod = load('render_desktop')
        for bad in ('/opt/a\nb/bin', '/opt/a\rb/bin', '/opt/a\x00b/bin', '/opt/a\tb/bin'):
            with self.assertRaises(ValueError):
                mod.render('Exec=@BINDIR@/x', bad)

    def test_installer_writes_valid_desktop_file_for_hostile_prefixes(self):
        for name in self.NASTY:
            with tempfile.TemporaryDirectory() as tmp:
                base = Path(tmp)
                prefix = base / name / 'win11-explorer-qt'
                fp = fake_prefix(base)
                shutil.copytree(fp, prefix, symlinks=True)
                home = base / 'home'
                home.mkdir()
                res = run_install(home, prefix)
                self.assertEqual(res.returncode, 0, (name, res.stderr))
                text = (home / '.local/share/applications/win11-explorer-qt.desktop').read_text()
                self.assertEqual(exec_argv(text), [f'{prefix}/bin/win11-explorer-qt', '%U'], name)
                if shutil.which('desktop-file-validate'):
                    v = subprocess.run(['desktop-file-validate', str(home / '.local/share/applications/win11-explorer-qt.desktop')],
                                       text=True, capture_output=True)
                    self.assertEqual(v.returncode, 0, (name, v.stdout, v.stderr))


class InstallerHardening(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.addCleanup(self.tmp.cleanup)
        self.base = Path(self.tmp.name)
        self.home = self.base / 'home'
        self.home.mkdir()
        self.prefix = fake_prefix(self.base)

    def test_home_must_be_a_real_absolute_directory_not_root(self):
        for bad in ('', '/', 'relative/home'):
            res = subprocess.run(['bash', str(INSTALL), '--prefix', str(self.prefix), '--dry-run'],
                                 env=dict(CLEAN_ENV, HOME=bad), text=True, capture_output=True)
            self.assertNotEqual(res.returncode, 0, repr(bad))
            self.assertIn('HOME', res.stderr, repr(bad))

    def test_empty_prefix_argument_is_an_error_not_the_default(self):
        res = run_install(self.home, Path(''), '--dry-run')
        self.assertNotEqual(res.returncode, 0)
        self.assertFalse((self.home / '.local').exists())

    def test_prefix_with_newline_is_rejected(self):
        res = run_install(self.home, Path('/opt/a\nExec=evil'), '--dry-run')
        self.assertNotEqual(res.returncode, 0)

    def test_foreign_symlink_launcher_is_not_overwritten(self):
        other = self.base / 'other' / 'bin'
        other.mkdir(parents=True)
        (other / 'win11-explorer-qt').write_text('somebody else')
        launcher = self.home / '.local/bin/win11-explorer-qt'
        launcher.parent.mkdir(parents=True)
        launcher.symlink_to(other / 'win11-explorer-qt')
        res = run_install(self.home, self.prefix)
        self.assertNotEqual(res.returncode, 0)
        self.assertEqual(os.readlink(launcher), str(other / 'win11-explorer-qt'))

    def test_foreign_desktop_and_icon_are_not_touched_by_install_or_uninstall(self):
        apps = self.home / '.local/share/applications'
        icons = self.home / '.local/share/icons/hicolor/scalable/apps'
        apps.mkdir(parents=True)
        icons.mkdir(parents=True)
        (apps / 'win11-explorer-qt.desktop').write_text('[Desktop Entry]\nName=mine\n')
        res = run_install(self.home, self.prefix)
        self.assertNotEqual(res.returncode, 0)
        self.assertEqual((apps / 'win11-explorer-qt.desktop').read_text(), '[Desktop Entry]\nName=mine\n')
        (icons / 'win11-explorer-qt.svg').write_text('<svg/>')
        run_install(self.home, self.prefix, '--uninstall')
        self.assertEqual((apps / 'win11-explorer-qt.desktop').read_text(), '[Desktop Entry]\nName=mine\n')
        self.assertEqual((icons / 'win11-explorer-qt.svg').read_text(), '<svg/>')

    def test_dry_run_uninstall_removes_nothing(self):
        self.assertEqual(run_install(self.home, self.prefix).returncode, 0)
        res = run_install(self.home, self.prefix, '--uninstall', '--dry-run')
        self.assertEqual(res.returncode, 0, res.stderr)
        self.assertIn('[dry-run] remove', res.stdout)
        self.assertTrue((self.home / '.local/share/applications/win11-explorer-qt.desktop').exists())

    @unittest.skipUnless(shutil.which('update-desktop-database'), 'desktop-file-utils missing')
    def test_install_refreshes_only_the_users_application_cache_so_open_with_works(self):
        res = run_install(self.home, self.prefix, '--dry-run')
        self.assertIn('update-desktop-database', res.stdout)
        self.assertFalse((self.home / '.local').exists())
        self.assertEqual(run_install(self.home, self.prefix).returncode, 0)
        cache = self.home / '.local/share/applications/mimeinfo.cache'
        self.assertIn('inode/directory=win11-explorer-qt.desktop;', cache.read_text())
        self.assertFalse((self.home / '.config').exists(), 'no mimeapps.list / default handler is written')
        run_install(self.home, self.prefix, '--uninstall')
        self.assertNotIn('win11-explorer-qt', cache.read_text() if cache.exists() else '')


class PatchelfPinned(unittest.TestCase):
    def test_requirements_pin_exact_version_with_hash(self):
        text = (ROOT / 'scripts/requirements-packaging.txt').read_text()
        self.assertRegex(text, r'(?m)^patchelf==\d+\.\d+\.\d+\.\d+ \\\n\s+--hash=sha256:[0-9a-f]{64}$')

    def test_unpinned_uv_fetch_is_gone(self):
        text = (ROOT / 'scripts/portable_prefix.py').read_text()
        self.assertNotIn("'--from', 'patchelf', 'patchelf'", text)
        self.assertIn('--require-hashes', text)

    def test_system_patchelf_is_used_and_described(self):
        mod = load('portable_prefix')
        with tempfile.TemporaryDirectory() as tmp:
            fake = Path(tmp) / 'patchelf'
            fake.write_text('#!/bin/sh\necho "patchelf 9.9.9"\n')
            fake.chmod(0o755)
            cmd, desc = mod.patchelf_cmd(ROOT, which=lambda name: str(fake))
            self.assertEqual(cmd, [str(fake)])
            self.assertIn('9.9.9', desc)
            self.assertIn(str(fake), desc)


class XcbRequired(unittest.TestCase):
    def fake_qt(self, tmp, plugins):
        qt = Path(tmp) / 'qt'
        for rel in plugins:
            f = qt / 'plugins' / rel
            f.parent.mkdir(parents=True, exist_ok=True)
            f.write_bytes(b'x')
        return qt

    def test_prefix_without_xcb_platform_plugin_is_an_error(self):
        mod = load('portable_prefix')
        with tempfile.TemporaryDirectory() as tmp:
            qt = self.fake_qt(tmp, ['platforms/libqoffscreen.so'])
            plugins, skipped = mod.collect_plugins(qt, '', ldd_fn=lambda p, ld: ({}, []))
            with self.assertRaises(SystemExit):
                mod.require_platform_plugins(plugins)

    def test_xcb_plugin_with_unresolved_dependency_is_an_error_not_a_skip(self):
        mod = load('portable_prefix')
        with tempfile.TemporaryDirectory() as tmp:
            qt = self.fake_qt(tmp, ['platforms/libqxcb.so', 'platforms/libqoffscreen.so', 'imageformats/libqjpeg.so'])
            def fake_ldd(path, ld):
                if path.name in ('libqxcb.so', 'libqjpeg.so'):
                    return {}, ['libxcb-cursor.so.0']
                return {}, []
            with self.assertRaises(SystemExit):
                mod.collect_plugins(qt, '', ldd_fn=fake_ldd)

    def test_optional_plugin_with_missing_dependency_is_skipped_and_reported(self):
        mod = load('portable_prefix')
        with tempfile.TemporaryDirectory() as tmp:
            qt = self.fake_qt(tmp, ['platforms/libqxcb.so', 'imageformats/libqjpeg.so'])
            fake_ldd = lambda path, ld: ({}, ['libjpeg.so.8']) if path.name == 'libqjpeg.so' else ({}, [])
            plugins, skipped = mod.collect_plugins(qt, '', ldd_fn=fake_ldd)
            self.assertIn('platforms/libqxcb.so', [f'{s}/{p.name}' for s, p in plugins])
            self.assertEqual(skipped, [('imageformats/libqjpeg.so', ['libjpeg.so.8'])])

    def test_verify_relocation_checks_xcb_plugin_with_env_i_ldd(self):
        mod = load('verify_relocation')
        with tempfile.TemporaryDirectory() as tmp:
            prefix = Path(tmp)
            ok, report = mod.check_xcb_plugin(prefix)
            self.assertFalse(ok)
            self.assertIn('libqxcb.so', report)

    def test_verify_xcb_refuses_personal_displays_before_any_input(self):
        script = ROOT / 'scripts/verify-xcb.sh'
        self.assertTrue(script.is_file())
        for d in (':0', ':1', ':0.0', ':1.0', '', 'localhost:0'):
            res = subprocess.run(['bash', str(script), '/nonexistent', d], env=CLEAN_ENV, text=True, capture_output=True)
            self.assertNotEqual(res.returncode, 0, repr(d))
            self.assertNotIn('prefix not found', res.stderr, repr(d))  # refusal happens before anything else
            self.assertIn('display', res.stderr.lower(), repr(d))


class PrefixGuardsPhysical(unittest.TestCase):
    def test_empty_home_without_prefix_is_refused_by_the_script(self):
        res = subprocess.run(['bash', str(ROOT / 'scripts/make-portable-prefix.sh')],
                             env=dict(CLEAN_ENV, HOME=''), text=True, capture_output=True)
        self.assertNotEqual(res.returncode, 0)
        self.assertIn('HOME', res.stderr)
        self.assertFalse(Path('/.local/opt').exists())

    def test_check_prefix_resolves_symlinks_before_guards(self):
        mod = load('portable_prefix')
        with tempfile.TemporaryDirectory() as tmp:
            link = Path(tmp) / 'x'
            link.symlink_to(ROOT / '.state')
            for bad_home in ('', '/'):
                with self.assertRaises(SystemExit):
                    mod.check_prefix(str(Path(tmp) / 'a/b/c'), ROOT, bad_home)
            with self.assertRaises(SystemExit):
                mod.check_prefix(str(link / 'p'), ROOT, tmp)
            self.assertFalse((ROOT / '.state/p').exists())
            ok = mod.check_prefix(str(Path(tmp) / 'sub/dir'), ROOT, tmp)
            self.assertEqual(ok, (Path(tmp).resolve() / 'sub/dir'))

    def test_prefix_equal_to_install_dir_or_overlay_is_refused(self):
        mod = load('portable_prefix')
        with tempfile.TemporaryDirectory() as tmp:
            with self.assertRaises(SystemExit):
                mod.check_prefix(str(Path(tmp) / 'a/b'), ROOT, tmp, protect=[Path(tmp) / 'a'])


class AtomicAssembly(unittest.TestCase):
    def test_failed_assembly_keeps_the_previous_prefix_intact(self):
        with tempfile.TemporaryDirectory() as tmp:
            tmp = Path(tmp)
            prefix = tmp / 'a' / 'prefix'
            (prefix / 'bin').mkdir(parents=True)
            (prefix / 'bin/win11-explorer-qt').write_text('previous working install')
            (prefix / '.win11-explorer-qt-prefix').write_text('created by scripts/make-portable-prefix.sh\n')
            install, overlay = tmp / 'install', tmp / 'overlay'
            (install / 'bin').mkdir(parents=True)
            (install / 'bin/win11-explorer-qt').write_text('not an ELF: assembly must fail midway')
            (overlay / 'lib').mkdir(parents=True)
            (overlay / 'lib/libfm-qt6.so.17').write_text('x')
            res = subprocess.run(['bash', str(ROOT / 'scripts/make-portable-prefix.sh'), '--prefix', str(prefix),
                                  '--install-dir', str(install), '--overlay', str(overlay)],
                                 env=dict(CLEAN_ENV, HOME=str(tmp)), text=True, capture_output=True)
            self.assertNotEqual(res.returncode, 0, res.stdout)
            self.assertEqual((prefix / 'bin/win11-explorer-qt').read_text(), 'previous working install')
            self.assertEqual(sorted(p.name for p in (tmp / 'a').iterdir()), ['prefix'], 'no staging leftovers')


class LicenseDocs(unittest.TestCase):
    def test_license_docs_installed_for_redistributed_components(self):
        pkg = load('pkg_licenses')
        with tempfile.TemporaryDirectory() as tmp:
            dest = Path(tmp) / 'doc'
            pkg.install_license_docs(dest, ROOT, Path(tmp) / 'no-qt-license-here', menu_cache_copyright=None)
            self.assertEqual(pkg.missing_docs(dest), [])
            for name in pkg.REQUIRED_DOCS:
                self.assertGreater((dest / name).stat().st_size, 200, name)
            self.assertIn('GNU LESSER GENERAL PUBLIC LICENSE', (dest / 'LICENSE.Qt-LGPL-3.0').read_text().upper())
            self.assertIn('Version 3', (dest / 'LICENSE.Qt-GPL-3.0').read_text())
            notices = (dest / 'THIRD_PARTY_NOTICES.md').read_text()
            for needle in ('Qt 6.8.3', 'ICU', 'libmenu-cache', 'libfm-qt', 'download.qt.io', 'relink', 'LGPL-3.0'):
                self.assertIn(needle, notices)

    def test_missing_license_text_is_a_hard_error(self):
        pkg = load('pkg_licenses')
        with tempfile.TemporaryDirectory() as tmp:
            with self.assertRaises(SystemExit):
                pkg.install_license_docs(Path(tmp) / 'doc', ROOT, Path(tmp) / 'q', menu_cache_copyright=None,
                                         common_licenses=Path(tmp) / 'empty')

    def test_missing_docs_reports_what_is_absent(self):
        pkg = load('pkg_licenses')
        with tempfile.TemporaryDirectory() as tmp:
            self.assertEqual(sorted(pkg.missing_docs(Path(tmp))), sorted(pkg.REQUIRED_DOCS))


def apt_known(name):
    out = subprocess.run(['apt-cache', 'policy', name], text=True, capture_output=True).stdout
    return bool(re.search(r'(?m)^\s+Candidato: (?!\(nenhum\))\S|^\s+Candidate: (?!\(none\))\S', out))


class DebPackage(unittest.TestCase):
    ALLOWED = {'libc6', 'libstdc++6', 'libglib2.0-0t64', 'libmenu-cache3', 'libxcb-cursor0', 'libxkbcommon-x11-0',
               'libxkbcommon0', 'libgl1', 'libegl1', 'libfontconfig1', 'libfreetype6', 'libdbus-1-3', 'libx11-xcb1',
               'libxcb-icccm4', 'libxcb-image0', 'libxcb-keysyms1', 'libxcb-render-util0', 'libxcb-shape0',
               'libxcb-sync1', 'libxcb-xfixes0', 'libxcb-xinerama0', 'libxcb-randr0', 'libxcb-xkb1', 'libharfbuzz0b',
               'libpng16-16t64', 'zlib1g', 'libgtk-3-0t64', 'gvfs'}

    def build(self, tmp, with_docs=True, extra=None):
        prefix = fake_prefix(Path(tmp))
        (prefix / 'lib').mkdir()
        (prefix / 'lib/libmenu-cache.so.3').write_text('bundled copy hardcodes /usr/lib/menu-cache')
        (prefix / 'lib/libQt6Core.so.6').write_text('qt')
        (prefix / 'libexec').mkdir()
        shutil.copy2('/usr/bin/ls', prefix / 'libexec/win11-explorer-qt')
        for rel in (extra or []):
            shutil.copy2(rel, prefix / 'lib' / Path(rel).name)
        if with_docs:
            add_docs(prefix)
        out = Path(tmp) / 'out'
        res = subprocess.run(['bash', str(ROOT / 'scripts/make-deb.sh'), '--prefix', str(prefix), '--out', str(out)],
                             env=dict(CLEAN_ENV, HOME=tmp), text=True, capture_output=True)
        return res, out

    def deb(self, out):
        debs = list(out.glob('win11-explorer-qt_*_amd64.deb'))
        self.assertEqual(len(debs), 1, debs)
        return debs[0]

    def control(self, deb):
        return subprocess.run(['dpkg-deb', '-f', str(deb)], text=True, capture_output=True).stdout

    def test_every_depends_name_exists(self):
        with tempfile.TemporaryDirectory() as tmp:
            res, out = self.build(tmp)
            self.assertEqual(res.returncode, 0, res.stderr)
            ctl = self.control(self.deb(out))
            depends = re.search(r'(?m)^Depends: (.*)$', ctl).group(1)
            names = [re.split(r'[ (]', d.strip())[0] for d in depends.split(',')]
            self.assertIn('libmenu-cache3', names)
            self.assertNotIn('menu-cached', names)
            for n in names:
                self.assertIn(n, self.ALLOWED, f'{n} is not in the verified allow-list')
                if shutil.which('apt-cache'):
                    self.assertTrue(apt_known(n), f'apt-cache does not know package {n}')

    def test_depends_glibc_floor_is_derived_from_the_elf_symbols(self):
        with tempfile.TemporaryDirectory() as tmp:
            res, out = self.build(tmp)
            self.assertEqual(res.returncode, 0, res.stderr)
            dump = subprocess.run(['objdump', '-T', '/usr/bin/ls'], text=True, capture_output=True).stdout
            vers = [tuple(int(x) for x in v.split('.')) for v in re.findall(r'\*UND\*.*\(GLIBC_(\d+(?:\.\d+)+)\)', dump)]
            top = '.'.join(str(x) for x in max(vers))
            self.assertIn(f'libc6 (>= {top})', self.control(self.deb(out)))

    def test_depends_libstdcxx_floor_follows_glibcxx_symbols(self):
        cand = list((ROOT / '.deps/Qt/6.8.3/gcc_64/lib').glob('libQt6Core.so.6.8.3'))
        if not cand:
            self.skipTest('Qt SDK not present')
        with tempfile.TemporaryDirectory() as tmp:
            res, out = self.build(tmp, extra=[str(cand[0])])
            self.assertEqual(res.returncode, 0, res.stderr)
            self.assertRegex(self.control(self.deb(out)), r'libstdc\+\+6 \(>= \d+(\.\d+)*\)')

    def test_deb_has_system_paths_docs_and_no_dev_tree(self):
        with tempfile.TemporaryDirectory() as tmp:
            res, out = self.build(tmp)
            self.assertEqual(res.returncode, 0, res.stderr)
            deb = self.deb(out)
            listing = subprocess.run(['dpkg-deb', '-c', str(deb)], text=True, capture_output=True).stdout
            self.assertIn('./opt/win11-explorer-qt/bin/win11-explorer-qt', listing)
            self.assertIn('./usr/bin/win11-explorer-qt -> /opt/win11-explorer-qt/bin/win11-explorer-qt', listing)
            self.assertIn('./opt/win11-explorer-qt/lib/libQt6Core.so.6', listing)
            self.assertNotIn('libmenu-cache.so', listing, 'the system libmenu-cache3 must be used')
            self.assertIn('./usr/share/doc/win11-explorer-qt/copyright', listing)
            for doc in load('pkg_licenses').REQUIRED_DOCS:
                self.assertIn(f'./usr/share/doc/win11-explorer-qt/{doc}', listing)
            owners = {line.split()[1] for line in listing.splitlines() if line.strip()}
            self.assertEqual(owners, {'root/root'})
            modes = [line.split()[0] for line in listing.splitlines() if line.strip()]
            self.assertFalse([m for m in modes if re.search(r'[sStT]', m[1:])], 'no setuid/setgid/sticky bits')
            self.assertFalse([m for m in modes if not m.startswith('l') and m[7] != 'r'], 'all files world-readable')
            maint = subprocess.run(['dpkg-deb', '-e', str(deb), str(Path(tmp) / 'ctl')], text=True, capture_output=True)
            self.assertEqual(maint.returncode, 0, maint.stderr)
            self.assertEqual(sorted(p.name for p in (Path(tmp) / 'ctl').iterdir()), ['control', 'md5sums'])
            fsys = subprocess.run(['dpkg-deb', '--fsys-tarfile', str(deb)], capture_output=True).stdout
            def member(name):
                return subprocess.run(['tar', '-xO', name], input=fsys, capture_output=True).stdout.decode()
            self.assertEqual(exec_argv(member('./usr/share/applications/win11-explorer-qt.desktop')),
                             ['/opt/win11-explorer-qt/bin/win11-explorer-qt', '%U'])
            copyright_text = member('./usr/share/doc/win11-explorer-qt/copyright')
            for needle in ('GPL-2', 'LGPL-3', 'Qt', 'ICU', 'libfm-qt', 'Format:'):
                self.assertIn(needle, copyright_text)

    def test_deb_refuses_a_prefix_without_license_docs(self):
        with tempfile.TemporaryDirectory() as tmp:
            res, out = self.build(tmp, with_docs=False)
            self.assertNotEqual(res.returncode, 0)
            self.assertIn('license', res.stderr.lower())
            self.assertEqual(list(out.glob('*.deb')) if out.exists() else [], [])

    def test_version_is_validated(self):
        with tempfile.TemporaryDirectory() as tmp:
            prefix = fake_prefix(Path(tmp))
            add_docs(prefix)
            for bad in ('1.0\nDepends: evil', '../x', 'a b'):
                res = subprocess.run(['bash', str(ROOT / 'scripts/make-deb.sh'), '--prefix', str(prefix),
                                      '--out', str(Path(tmp) / 'o'), '--version', bad],
                                     env=dict(CLEAN_ENV, HOME=tmp), text=True, capture_output=True)
                self.assertNotEqual(res.returncode, 0, repr(bad))


class Docs(unittest.TestCase):
    def test_install_md_documents_requirements_licenses_and_debug_path(self):
        text = (ROOT / 'docs/INSTALL.md').read_text()
        for needle in ('Requisitos de build', 'patchelf==', 'rede', 'THIRD_PARTY_NOTICES', 'debug-file-directory',
                       'verify-xcb.sh', 'update-desktop-database', 'libmenu-cache3'):
            self.assertIn(needle, text)
        for src in (text, (ROOT / 'scripts/make-deb.sh').read_text()):
            self.assertIsNone(re.search(r'Depends:[^\n]*\bmenu-cached\b', src), 'no stale menu-cached Depends')


if __name__ == '__main__':
    unittest.main()
