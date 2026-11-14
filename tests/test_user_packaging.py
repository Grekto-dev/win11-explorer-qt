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
import sys
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'tests'))
import deb_completeness  # noqa: E402
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
        if name == 'LICENSE.ICU':  # make-deb.sh re-checks the pinned sha256, so a fake text would be refused
            shutil.copy2(ROOT / 'packaging/licenses/LICENSE.ICU', dest / name)
        else:
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

    def test_system_patchelf_is_searched_only_in_trusted_dirs_and_hashed(self):
        import hashlib
        mod = load('portable_prefix')
        seen = []
        def which(name, path=None):
            seen.append(path)
            return '/usr/bin/env' if name == 'patchelf' else None  # stands in for /usr/bin/patchelf
        cmd, desc = mod.patchelf_cmd(ROOT, which=which)
        self.assertEqual(seen[0], '/usr/bin:/bin', 'the caller PATH (~/.local/bin, writable dirs) must not be searched')
        self.assertEqual(cmd, ['/usr/bin/env'])
        self.assertIn('/usr/bin/env', desc)
        self.assertIn('sha256 ' + hashlib.sha256(Path('/usr/bin/env').read_bytes()).hexdigest(), desc)

    def test_patchelf_outside_usr_is_ignored_even_if_found(self):
        mod = load('portable_prefix')
        with tempfile.TemporaryDirectory() as tmp:
            fake = Path(tmp) / 'patchelf'
            fake.write_text('#!/bin/sh\necho "patchelf 9.9.9"\n')
            fake.chmod(0o755)
            tools = ROOT / '.state/tools/patchelf-0.19.3.0/bin/patchelf'
            if not tools.is_file():
                self.skipTest('pinned patchelf not present (would need the network)')
            cmd, desc = mod.patchelf_cmd(ROOT, which=lambda name, path=None: str(fake))
            self.assertNotEqual(cmd, [str(fake)])
            self.assertNotIn('9.9.9', desc)
            self.assertIn('pinned', desc)
            self.assertRegex(desc, r'sha256 [0-9a-f]{64}')


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


class VerifyXcbOwner(unittest.TestCase):
    """The X server ownership check, against fake servers that listen on the path socket AND the abstract
    socket (the one libxcb really connects to first). Only the owner check runs (VERIFY_XCB_OWNER_CHECK_ONLY)."""
    NUM = 2000 + os.getpid() % 5000
    SERVER = (
        'import os, socket, sys, time\n'
        'd, n, abstract = sys.argv[1], sys.argv[2], sys.argv[3] == "1"\n'
        'socks = []\n'
        'p = socket.socket(socket.AF_UNIX); p.bind(f"{d}/X{n}"); p.listen(1); socks.append(p)\n'
        'if abstract:\n'
        '    a = socket.socket(socket.AF_UNIX); a.bind(f"\\0{d}/X{n}"); a.listen(1); socks.append(a)\n'
        'print("ready", flush=True); time.sleep(60)\n')

    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.addCleanup(self.tmp.cleanup)
        self.dir = Path(self.tmp.name)
        self.procs = []
        self.addCleanup(self.stop)

    def stop(self):
        for p in self.procs:
            p.kill()
            p.wait()
            p.stdout.close()

    def spawn(self, exe_name, cmdline_decoy='', abstract=True):
        exe = self.dir / exe_name
        shutil.copy2(os.path.realpath('/usr/bin/python3'), exe)
        argv = [str(exe), '-c', self.SERVER, str(self.dir), str(self.NUM), '1' if abstract else '0']
        if cmdline_decoy:
            argv.append(cmdline_decoy)  # an arbitrary extra argument that merely MENTIONS Xephyr
        p = subprocess.Popen(argv, stdout=subprocess.PIPE, text=True, env=dict(CLEAN_ENV, PYTHONHOME='', LC_ALL='C'))
        self.procs.append(p)
        self.assertEqual(p.stdout.readline().strip(), 'ready')
        return p

    def check(self, path=None):
        env = dict(CLEAN_ENV, **({'PATH': path} if path else {}), VERIFY_XCB_SOCKET_DIR=str(self.dir), VERIFY_XCB_OWNER_CHECK_ONLY='1')
        prefix = self.dir / 'prefix'
        (prefix / 'bin').mkdir(parents=True, exist_ok=True)
        (prefix / 'bin/win11-explorer-qt').write_text('#!/bin/sh\n')
        (prefix / 'bin/win11-explorer-qt').chmod(0o755)
        return subprocess.run(['bash', str(ROOT / 'scripts/verify-xcb.sh'), str(prefix), f':{self.NUM}'],
                              env=env, text=True, capture_output=True)

    def test_a_real_nested_server_executable_is_accepted(self):
        # the exe is called Xvfb but the cmdline says nothing about it: only /proc/PID/exe is trusted
        pid = self.spawn('Xvfb').pid
        res = self.check()
        # the owner check passed, but NO app ran: that must not look like success (exit 0 / PASS)
        self.assertEqual(res.returncode, 3, res.stderr)
        self.assertIn('OWNER-CHECK-ONLY (no app run)', res.stdout)
        self.assertNotIn('PASS', res.stdout)
        self.assertIn(f'pid {pid}', res.stdout)

    def test_abstract_socket_without_a_visible_pid_is_refused_fail_closed(self):
        # an abstract socket exists but `ss` shows no users:(..pid=..) for it (other uid): refuse, do not continue
        self.spawn('Xvfb')
        fake = self.dir / 'fakebin'
        fake.mkdir()
        (fake / 'ss').write_text('#!/bin/sh\n/usr/bin/ss "$@" | awk \'$5 ~ /^@/ {sub(/users:.*/, "")} {print}\'\n')
        (fake / 'ss').chmod(0o755)
        res = self.check(path=f'{fake}:/usr/bin:/bin')
        self.assertNotIn(res.returncode, (0, 3), (res.stdout, res.stderr))
        self.assertIn('abstract', res.stderr)
        self.assertNotIn('OWNER-CHECK-ONLY', res.stdout)

    def test_cmdline_that_merely_mentions_xephyr_is_not_enough(self):
        self.spawn('someprog', cmdline_decoy='Xephyr')
        res = self.check()
        self.assertNotEqual(res.returncode, 0, res.stdout)
        self.assertIn('not a nested', res.stderr)

    def test_abstract_socket_held_by_another_process_is_refused(self):
        pid_path = self.spawn('Xvfb', abstract=False).pid
        # a different process listens on the abstract name
        other = subprocess.Popen([sys.executable, '-c',
                                  'import socket,time,sys\n'
                                  f'a=socket.socket(socket.AF_UNIX); a.bind("\\0{self.dir}/X{self.NUM}"); a.listen(1)\n'
                                  'print("ready", flush=True); time.sleep(60)'], stdout=subprocess.PIPE, text=True)
        self.procs.append(other)
        self.assertEqual(other.stdout.readline().strip(), 'ready')
        res = self.check()
        self.assertNotEqual(res.returncode, 0, (pid_path, res.stdout))
        self.assertIn('abstract', res.stderr)


class RelocationCoversOriginalPrefix(unittest.TestCase):
    def setUp(self):
        self.mod = load('verify_relocation')

    def test_original_prefix_is_hidden_under_tmpfs_too(self):
        with tempfile.TemporaryDirectory() as tmp:
            src = Path(tmp).resolve() / 'orig prefix'
            src.mkdir()
            hidden = self.mod.hidden_paths(src)
            self.assertIn(str(src), hidden)
            self.assertIn(str((ROOT / '.state').resolve()) if (ROOT / '.state').is_dir() else str(src), hidden)

    def test_reloc_proof_fails_when_the_original_prefix_shows_up_in_maps_or_strace(self):
        src = '/home/u/.local/opt/win11-explorer-qt'
        maps = ['/home/u/.local/opt/win11-explorer-qt/lib/libQt6Core.so.6', '/tmp/moved/lib/libQt6Gui.so.6']
        self.assertEqual(self.mod.src_in_maps(maps, src), [maps[0]])
        self.assertEqual(self.mod.src_in_maps(maps[1:], src), [])
        self.assertEqual(self.mod.src_in_maps(['/home/u/.local/opt/win11-explorer-qt-other/lib/x.so'], src), [])
        strace = ('123 openat(AT_FDCWD, "/home/u/.local/opt/win11-explorer-qt/share/x.qm", O_RDONLY) = -1 ENOENT (No such file)\n'
                  '123 openat(AT_FDCWD, "/tmp/moved/share/y.qm", O_RDONLY) = 3\n'
                  '123 statx(AT_FDCWD, "/home/u/.local/opt/win11-explorer-qt", 0) = -1 ENOENT\n'
                  '123 openat(AT_FDCWD, "/home/u/.local/opt/win11-explorer-qt-other/z", O_RDONLY) = 3\n')
        self.assertEqual(self.mod.src_in_strace(strace, src),
                         ['/home/u/.local/opt/win11-explorer-qt', '/home/u/.local/opt/win11-explorer-qt/share/x.qm'])

    def test_workdir_inside_or_containing_the_original_prefix_is_refused(self):
        with tempfile.TemporaryDirectory() as tmp:
            src = Path(tmp).resolve() / 'p'
            (src / 'work').mkdir(parents=True)
            self.assertTrue(self.mod.overlaps_hidden(src / 'work', [str(src)]))
            self.assertTrue(self.mod.overlaps_hidden(Path(tmp).resolve(), [str(src)]))
            self.assertFalse(self.mod.overlaps_hidden(Path(tmp).resolve() / 'other', [str(src)]))


class VerifyXcbCleanup(unittest.TestCase):
    def test_app_is_started_in_its_own_session_and_the_whole_group_is_killed(self):
        text = (ROOT / 'scripts/verify-xcb.sh').read_text()
        self.assertIn('setsid', text)
        self.assertRegex(text, r'kill [^\n]*-- "?-\$?\{?app')
        self.assertNotIn('kill "$app"', text)


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


MARKER_TEXT = 'created by scripts/make-portable-prefix.sh\n'


def make_prefix_dir(path: Path, text='x'):
    (path / 'bin').mkdir(parents=True)
    (path / 'bin/win11-explorer-qt').write_text(text)
    (path / '.win11-explorer-qt-prefix').write_text(MARKER_TEXT)
    return path


def dead_pid():
    pid = 4_000_000
    while True:
        try:
            os.kill(pid, 0)
            pid += 1
        except ProcessLookupError:
            return pid
        except PermissionError:
            pid += 1


class SwapRobustness(unittest.TestCase):
    def setUp(self):
        self.mod = load('portable_prefix')
        self.tmp = tempfile.TemporaryDirectory()
        self.addCleanup(self.tmp.cleanup)
        self.dir = Path(self.tmp.name) / 'a'
        self.dir.mkdir()
        self.prefix = self.dir / 'prefix'
        self.prev = self.dir / '.prefix.prev'

    def stage(self, pid=None):
        return make_prefix_dir(self.dir / f'.prefix.new.{pid or os.getpid()}', 'new')

    def test_publish_replaces_prefix_and_drops_prev(self):
        make_prefix_dir(self.prefix, 'old')
        stage = self.stage()
        self.mod.publish(self.prefix, stage, self.prev)
        self.assertEqual((self.prefix / 'bin/win11-explorer-qt').read_text(), 'new')
        self.assertEqual(sorted(p.name for p in self.dir.iterdir()), ['prefix'])

    def test_prev_without_marker_is_never_removed_and_stage_is_cleaned(self):
        make_prefix_dir(self.prefix, 'old')
        self.prev.mkdir()
        (self.prev / 'precious').write_text('keep')
        stage = self.stage()
        with self.assertRaises(SystemExit):
            self.mod.publish(self.prefix, stage, self.prev)
        self.assertEqual((self.prev / 'precious').read_text(), 'keep')
        self.assertEqual((self.prefix / 'bin/win11-explorer-qt').read_text(), 'old')
        self.assertFalse(stage.exists(), 'stage must not be left behind')

    def test_prev_symlink_is_refused_not_followed_and_stage_is_cleaned(self):
        make_prefix_dir(self.prefix, 'old')
        target = Path(self.tmp.name) / 'elsewhere'
        make_prefix_dir(target, 'other')
        self.prev.symlink_to(target)
        stage = self.stage()
        with self.assertRaises(SystemExit):
            self.mod.publish(self.prefix, stage, self.prev)
        self.assertTrue((target / 'bin/win11-explorer-qt').is_file())
        self.assertFalse(stage.exists())

    def test_failure_of_the_first_rename_cleans_stage_and_keeps_prefix(self):
        make_prefix_dir(self.prefix, 'old')
        stage = self.stage()
        real = os.rename
        def boom(src, dst):
            if Path(src) == self.prefix:
                raise OSError(16, 'Device or resource busy')
            return real(src, dst)
        self.mod.os.rename = boom
        self.addCleanup(setattr, self.mod.os, 'rename', real)
        with self.assertRaises(OSError):
            self.mod.publish(self.prefix, stage, self.prev)
        self.mod.os.rename = real
        self.assertEqual((self.prefix / 'bin/win11-explorer-qt').read_text(), 'old')
        self.assertFalse(stage.exists())
        self.assertFalse(self.prev.exists())

    def test_failure_of_the_second_rename_restores_prefix_and_cleans_stage(self):
        make_prefix_dir(self.prefix, 'old')
        stage = self.stage()
        real = os.rename
        def boom(src, dst):
            if Path(src) == stage:
                raise OSError(18, 'Invalid cross-device link')
            return real(src, dst)
        self.mod.os.rename = boom
        self.addCleanup(setattr, self.mod.os, 'rename', real)
        with self.assertRaises(OSError):
            self.mod.publish(self.prefix, stage, self.prev)
        self.mod.os.rename = real
        self.assertEqual((self.prefix / 'bin/win11-explorer-qt').read_text(), 'old')
        self.assertFalse(stage.exists())
        self.assertFalse(self.prev.exists())

    def test_orphan_stages_of_dead_pids_are_removed_live_ones_kept(self):
        dead = self.stage(dead_pid())
        live = self.stage(os.getppid())
        unrelated = self.dir / '.prefix.new.notapid'
        unrelated.mkdir()
        foreign = self.dir / ('.other.new.%d' % dead_pid())
        foreign.mkdir()
        self.mod.clean_orphans(self.prefix, self.prev)
        self.assertFalse(dead.exists())
        self.assertTrue(live.exists())
        self.assertTrue(unrelated.exists())
        self.assertTrue(foreign.exists())

    def test_orphan_stage_with_an_impossible_huge_pid_is_ignored_not_a_crash(self):
        # os.kill(10**23, 0) raises OverflowError: that used to abort EVERY later run of make-portable-prefix.sh
        huge = self.stage('9' * 23)
        huge_unmarked = self.dir / ('.prefix.new.' + '9' * 40)
        huge_unmarked.mkdir()
        (huge_unmarked / 'precious').write_text('keep')
        dead = self.stage(dead_pid())
        self.mod.clean_orphans(self.prefix, self.prev)  # must not raise
        self.assertTrue(huge.exists(), 'a directory whose pid is invalid is not ours to delete')
        self.assertEqual((huge_unmarked / 'precious').read_text(), 'keep')
        self.assertFalse(dead.exists(), 'the real orphan is still cleaned')
        self.assertFalse(self.mod.pid_alive(10 ** 23))

    def test_publish_validates_prev_even_when_the_prefix_does_not_exist(self):
        self.prev.mkdir()
        (self.prev / 'precious').write_text('keep')
        stage = self.stage()
        with self.assertRaises(SystemExit):
            self.mod.publish(self.prefix, stage, self.prev)
        self.assertEqual((self.prev / 'precious').read_text(), 'keep')
        self.assertFalse(stage.exists())

    def test_orphan_stage_symlink_is_not_followed(self):
        target = Path(self.tmp.name) / 'elsewhere'
        make_prefix_dir(target, 'other')
        link = self.dir / f'.prefix.new.{dead_pid()}'
        link.symlink_to(target)
        self.mod.clean_orphans(self.prefix, self.prev)
        self.assertTrue((target / 'bin/win11-explorer-qt').is_file())

    def test_lone_prev_left_by_a_kill_in_the_swap_window_is_restored(self):
        make_prefix_dir(self.prev, 'old')
        self.mod.clean_orphans(self.prefix, self.prev)
        self.assertEqual((self.prefix / 'bin/win11-explorer-qt').read_text(), 'old')
        self.assertFalse(self.prev.exists())

    def test_lone_prev_without_marker_is_not_touched(self):
        self.prev.mkdir()
        (self.prev / 'precious').write_text('keep')
        self.mod.clean_orphans(self.prefix, self.prev)
        self.assertEqual((self.prev / 'precious').read_text(), 'keep')
        self.assertFalse(self.prefix.exists())


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


# icu4c/LICENSE of tag release-73-2 (https://github.com/unicode-org/icu), fetched and pinned.
ICU_73_2_LICENSE_SHA256 = 'f3005e195ff74d8812cc1f182a1c446fab678d70a10e3dada497585befee5416'


class IcuLicenseAndCopyright(unittest.TestCase):
    def test_icu_license_is_the_pinned_upstream_73_2_text(self):
        import hashlib
        data = (ROOT / 'packaging/licenses/LICENSE.ICU').read_bytes()
        self.assertEqual(hashlib.sha256(data).hexdigest(), ICU_73_2_LICENSE_SHA256)
        self.assertEqual(load('pkg_licenses').ICU_LICENSE_SHA256, ICU_73_2_LICENSE_SHA256)
        text = data.decode('utf-8')
        for needle in ('Copyright \u00a9 1991-2023 Unicode, Inc.', 'Lao Word Break Dictionary Data',
                       'Burmese Word Break Dictionary Data', 'Google double-conversion', 'International Business Machines'):
            self.assertIn(needle, text)
        self.assertNotIn('debian/*', text, 'the Debian packager stanza is not part of the ICU license')

    def test_installer_refuses_a_license_text_that_is_not_the_pinned_one(self):
        pkg = load('pkg_licenses')
        with tempfile.TemporaryDirectory() as tmp:
            repo = Path(tmp) / 'repo'
            for rel in ('LICENSE', 'patches/libfm-qt/LICENSE.LGPL-2.1', 'packaging/THIRD_PARTY_NOTICES.md'):
                (repo / rel).parent.mkdir(parents=True, exist_ok=True)
                shutil.copy2(ROOT / rel, repo / rel)
            (repo / 'packaging/licenses').mkdir(parents=True)
            (repo / 'packaging/licenses/LICENSE.ICU').write_text('Copyright 1991-2022 the Debian libicu74 copyright file ' * 20)
            with self.assertRaises(SystemExit):
                pkg.install_license_docs(Path(tmp) / 'doc', repo, Path(tmp) / 'qt', menu_cache_copyright=None)

    def stanzas(self):
        text = (ROOT / 'packaging/debian-copyright').read_text()
        return [dict(re.findall(r'(?m)^([A-Za-z-]+): ?(.*)$', st)) for st in re.split(r'\n\s*\n', text.strip())]

    def test_debian_copyright_has_catch_all_and_standalone_license_stanzas(self):
        st = self.stanzas()
        self.assertIn('*', [x.get('Files', '').split()[0] for x in st if 'Files' in x], "needs a 'Files: *' stanza")
        defined = {x['License'].splitlines()[0].strip() for x in st if 'Files' not in x and 'License' in x}
        used = set()
        for x in st:
            if 'Files' in x:
                used |= {n.strip() for n in re.split(r'\band\b', x['License'].splitlines()[0])}
        self.assertEqual(sorted(used - defined), [], 'licenses used in Files stanzas without a standalone License stanza')

    def test_notices_do_not_claim_unmodified_binaries(self):
        for f in ('packaging/THIRD_PARTY_NOTICES.md', 'packaging/debian-copyright'):
            text = (ROOT / f).read_text()
            self.assertNotIn('unmodified', text, f)
            self.assertIn('RPATH', text, f)


def apt_known(name):
    out = subprocess.run(['apt-cache', 'policy', name], text=True, capture_output=True).stdout
    return bool(re.search(r'(?m)^\s+Candidato: (?!\(nenhum\))\S|^\s+Candidate: (?!\(none\))\S', out))


_REAL_DEB = {}


def ci_required(environ=None):
    environ = os.environ if environ is None else environ
    return bool(environ.get('CI') or environ.get('WIN11_REQUIRE_BUILD_TREE'))


def missing_build_inputs(root=ROOT, environ=None, which=shutil.which):
    """Why a real prefix cannot be assembled here ([] = it can). Explicit WIN11_TEST_PREFIX needs nothing."""
    environ = os.environ if environ is None else environ
    if environ.get('WIN11_TEST_PREFIX'):
        return []
    root = Path(root)
    why = []
    if not ((root / '.state/install/bin/win11-explorer-qt').is_file()
            and list((root / '.state/libfm-overlay-prefix/lib').glob('libfm-qt6.so*')) and (root / '.deps').is_dir()):
        why.append('build tree missing (.state/install, .state/libfm-overlay-prefix, .deps): run scripts/build.sh, '
                   'or point WIN11_TEST_PREFIX at an existing prefix')
    for tool in ('readelf', 'dpkg'):
        if not which(tool):
            why.append(f'{tool} not found')
    pe = load('portable_prefix')
    system_pe = which('patchelf', path=pe.TRUSTED_BIN_PATH)
    pinned = root / '.state/tools' / f'patchelf-{pe.PATCHELF_PIN}' / 'bin/patchelf'
    if not (system_pe and os.path.realpath(system_pe).startswith('/usr/')) and not pinned.is_file():
        why.append(f'patchelf not available (neither /usr/bin/patchelf nor {pinned}): populate .state/tools '
                   f'(see docs/INSTALL.md) or `sudo apt install patchelf`')
    return why


def require_inputs(case, why, environ=None):
    """The Depends completeness test must not pass silently: in CI (CI / WIN11_REQUIRE_BUILD_TREE) missing
    inputs are a FAILURE; elsewhere a skip whose reason says exactly what to provide."""
    if not why:
        return
    msg = 'Depends completeness NOT verified: ' + '; '.join(why)
    if ci_required(environ):
        case.fail(msg)
    case.skipTest(msg)


def real_prefix_deb(case):
    """A .deb built from a real prefix (WIN11_TEST_PREFIX, or assembled from .state/install + the overlay)."""
    if 'deb' in _REAL_DEB:
        return _REAL_DEB['deb']
    require_inputs(case, missing_build_inputs())
    tmp = tempfile.mkdtemp(prefix='p4-realdeb-')
    case.addClassCleanup(shutil.rmtree, tmp, True)
    prefix = os.environ.get('WIN11_TEST_PREFIX')
    if not prefix:
        home = Path(tmp) / 'home'
        home.mkdir()
        prefix = str(Path(tmp) / 'p' / 'win11-explorer-qt')
        res = subprocess.run(['bash', str(ROOT / 'scripts/make-portable-prefix.sh'), '--prefix', prefix],
                             env=dict(CLEAN_ENV, HOME=str(home)), text=True, capture_output=True)
        case.assertEqual(res.returncode, 0, f'make-portable-prefix.sh failed (a REAL failure, not a skip):\n{res.stderr}')
    out = Path(tmp) / 'deb'
    res = subprocess.run(['bash', str(ROOT / 'scripts/make-deb.sh'), '--prefix', prefix, '--out', str(out), '--without-debug'],
                         env=dict(CLEAN_ENV, HOME=tmp), text=True, capture_output=True)
    case.assertEqual(res.returncode, 0, f'make-deb.sh failed:\n{res.stderr}')
    _REAL_DEB['deb'] = next(out.glob('*.deb'))
    return _REAL_DEB['deb']


class CompletenessGate(unittest.TestCase):
    """The completeness test may not turn into a silent skip in CI, and a missing patchelf is explained."""
    def test_missing_build_tree_is_reported_with_what_to_do(self):
        with tempfile.TemporaryDirectory() as tmp:
            why = missing_build_inputs(Path(tmp), environ={})
        self.assertTrue(any('build tree missing' in w and 'WIN11_TEST_PREFIX' in w for w in why), why)

    def test_missing_patchelf_says_how_to_provide_it(self):
        with tempfile.TemporaryDirectory() as tmp:
            why = missing_build_inputs(Path(tmp), environ={}, which=lambda name, path=None: None if name == 'patchelf' else '/usr/bin/true')
        pe = [w for w in why if 'patchelf' in w]
        self.assertEqual(len(pe), 1, why)
        self.assertIn('.state/tools', pe[0])
        self.assertIn('apt install patchelf', pe[0])

    def test_explicit_prefix_needs_no_build_tree(self):
        self.assertEqual(missing_build_inputs(Path('/nonexistent'), environ={'WIN11_TEST_PREFIX': '/x'}), [])

    def test_ci_turns_missing_inputs_into_a_failure_not_a_skip(self):
        for env in ({'CI': 'true'}, {'WIN11_REQUIRE_BUILD_TREE': '1'}):
            with self.assertRaises(AssertionError) as cm:
                require_inputs(self, ['build tree missing'], environ=env)
            self.assertIn('Depends completeness NOT verified', str(cm.exception))
            self.assertIn('build tree missing', str(cm.exception))

    def test_outside_ci_it_is_a_skip_with_the_reason(self):
        with self.assertRaises(unittest.SkipTest) as cm:
            require_inputs(self, ['patchelf not available'], environ={})
        self.assertIn('patchelf not available', str(cm.exception))

    def test_nothing_missing_does_not_skip_or_fail(self):
        require_inputs(self, [], environ={'CI': '1'})


class DebPackage(unittest.TestCase):
    ALLOWED = {'libc6', 'libstdc++6', 'libglib2.0-0t64', 'libmenu-cache3', 'libxcb-cursor0', 'libxkbcommon-x11-0',
               'libxkbcommon0', 'libgl1', 'libegl1', 'libfontconfig1', 'libfreetype6', 'libdbus-1-3', 'libx11-xcb1',
               'libxcb-icccm4', 'libxcb-image0', 'libxcb-keysyms1', 'libxcb-render-util0', 'libxcb-shape0',
               'libxcb-sync1', 'libxcb-xfixes0', 'libxcb-xinerama0', 'libxcb-randr0', 'libxcb-xkb1', 'libharfbuzz0b',
               'libpng16-16t64', 'zlib1g', 'libgtk-3-0t64', 'gvfs', 'libopengl0', 'libgssapi-krb5-2',
               'libjpeg-turbo8'}

    def build(self, tmp, with_docs=True, extra=None, icu_text=None):
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
            if icu_text is not None:
                (prefix / 'share/doc/win11-explorer-qt/LICENSE.ICU').write_text(icu_text)
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

    def test_depends_cover_every_system_library_the_shipped_elfs_need(self):
        """Completeness, derived from the real prefix: NEEDED sonames the .deb does not carry ->
        ldconfig -> dpkg -S -> package, which must be in Depends or reachable from it
        (apt-cache depends --recurse). Not a list compared with itself."""
        deb = real_prefix_deb(self)
        res = deb_completeness.check_deb(deb)
        self.assertEqual(res['unresolved'], [], 'NEEDED sonames nothing on this host provides')
        self.assertEqual(res['hard'], {}, 'Depends lacks packages for sonames NEEDED by the binary/libs/required plugins')
        self.assertEqual(res['optional'], {}, 'Depends+Recommends lack packages for optional plugins')

    def test_completeness_check_reports_a_package_that_nothing_in_depends_reaches(self):
        needed_by = {'libopengl0': ['libOpenGL.so.0'], 'libgl1': ['libGL.so.1']}
        closure = lambda names: set(names) | {'libgl1'}
        self.assertEqual(deb_completeness.missing_packages(needed_by, ['libgl1'], closure),
                         {'libopengl0': ['libOpenGL.so.0']})
        self.assertEqual(deb_completeness.missing_packages(needed_by, ['libgl1', 'libopengl0'], lambda n: set(n)), {})

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

    def test_deb_refuses_a_prefix_whose_icu_license_is_not_the_pinned_text(self):
        with tempfile.TemporaryDirectory() as tmp:
            res, out = self.build(tmp, icu_text='Copyright 1991-2022 the Debian libicu74 copyright file ' * 20)
            self.assertNotEqual(res.returncode, 0)
            self.assertIn('LICENSE.ICU', res.stderr)
            self.assertIn('f3005e195ff74d8812cc1f182a1c446fab678d70a10e3dada497585befee5416', res.stderr)
            self.assertEqual(list(out.glob('*.deb')) if out.exists() else [], [])

    def test_deb_accepts_the_pinned_icu_license(self):
        with tempfile.TemporaryDirectory() as tmp:
            res, out = self.build(tmp)
            self.assertEqual(res.returncode, 0, res.stderr)

    def test_version_is_validated(self):
        with tempfile.TemporaryDirectory() as tmp:
            prefix = fake_prefix(Path(tmp))
            add_docs(prefix)
            for bad in ('1.0\nDepends: evil', '../x', 'a b'):
                res = subprocess.run(['bash', str(ROOT / 'scripts/make-deb.sh'), '--prefix', str(prefix),
                                      '--out', str(Path(tmp) / 'o'), '--version', bad],
                                     env=dict(CLEAN_ENV, HOME=tmp), text=True, capture_output=True)
                self.assertNotEqual(res.returncode, 0, repr(bad))


class DocsAccuracy(unittest.TestCase):
    def test_desktop_template_does_not_claim_the_installer_skips_update_desktop_database(self):
        text = (ROOT / 'packaging/win11-explorer-qt.desktop.in').read_text()
        self.assertNotIn('neither runs xdg-mime nor update-desktop-database', text)
        self.assertIn('xdg-mime', text)  # still states that mimeapps/xdg-mime are never touched
        self.assertIn('update-desktop-database', text)
        mod = load('render_desktop')
        self.assertIn('update-desktop-database', mod.render(text, '/opt/x/bin'))

    def test_install_md_says_dt_rpath_not_runpath_for_the_elf_files(self):
        text = (ROOT / 'docs/INSTALL.md').read_text()
        self.assertNotIn('RUNPATH', text)
        self.assertIn('DT_RPATH', text)
        self.assertNotIn('RUNPATH', (ROOT / 'packaging/win11-explorer-qt.sh').read_text())

    def test_install_md_documents_the_swap_window_recovery_and_closing_the_app(self):
        text = (ROOT / 'docs/INSTALL.md').read_text()
        for needle in ('.prev', '.new.', 'feche o app', 'recuper', 'janela'):
            self.assertIn(needle, text.lower())


class Docs(unittest.TestCase):
    def test_install_md_documents_requirements_licenses_and_debug_path(self):
        text = (ROOT / 'docs/INSTALL.md').read_text()
        for needle in ('Requisitos de build', 'patchelf==', 'rede', 'THIRD_PARTY_NOTICES', 'debug-file-directory',
                       'verify-xcb.sh', 'update-desktop-database', 'libmenu-cache3'):
            self.assertIn(needle, text)
        for src in (text, (ROOT / 'scripts/make-deb.sh').read_text()):
            self.assertIsNone(re.search(r'(?m)^Depends:[^\n]*\bmenu-cached\b', src), 'no stale menu-cached Depends')


if __name__ == '__main__':
    unittest.main()
