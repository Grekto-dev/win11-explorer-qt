#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Assemble a relocatable prefix from an already built tree (see make-portable-prefix.sh).

Layout:  bin/ (launcher)  libexec/ (ELF + qt.conf)  lib/ (private libfm-qt, Qt, ICU,
libs absent from the host)  plugins/  share/.  Only the prefix is written."""
import argparse, os, re, shutil, subprocess, sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import pkg_licenses  # noqa: E402

MARKER = '.win11-explorer-qt-prefix'
MARKER_TEXT = 'created by scripts/make-portable-prefix.sh\n'
# Without the xcb platform plugin (and every library it needs) the app cannot open a window on
# the Mint/X11 desktop, so its absence is an ERROR, never a silent skip.
REQUIRED_PLUGINS = [('platforms', 'libqxcb.so')]
PLUGIN_ALLOW = {
    'platforms': ['libqxcb.so', 'libqoffscreen.so', 'libqminimal.so'],
    'xcbglintegrations': None, 'platformthemes': None, 'imageformats': None,
    'iconengines': None, 'tls': None, 'platforminputcontexts': None, 'styles': None,
}
SYSTEM_LIB_DIRS = ('/lib/', '/usr/lib/', '/lib64/', '/usr/lib64/')
# Pinned build tool (hash-checked wheel); a system patchelf on PATH is preferred.
PATCHELF_PIN = '0.19.3.0'


def die(msg):
    print(f'error: {msg}', file=sys.stderr)
    sys.exit(1)


def run(cmd, **kw):
    return subprocess.run(cmd, text=True, capture_output=True, **kw)


def ldd(path, ld_path):
    env = {'PATH': '/usr/bin:/bin', 'LD_LIBRARY_PATH': ld_path} if ld_path is not None else {'PATH': '/usr/bin:/bin'}
    out = run(['ldd', str(path)], env=env).stdout
    found, missing = {}, []
    for line in out.splitlines():
        m = re.match(r'\s*(\S+) => (\S+) \(0x', line)
        if m:
            found[m.group(1)] = m.group(2)
        elif '=> not found' in line:
            missing.append(line.split()[0])
    return found, missing


def system_sonames():
    out = run(['/sbin/ldconfig', '-p']).stdout
    return {line.split()[0] for line in out.splitlines()[1:] if line.strip()}


def patchelf_cmd(repo, which=shutil.which):
    """(command, description). Prefer a system patchelf; else a hash-pinned wheel installed into
    <repo>/.state/tools (needs network and uv once; see docs/INSTALL.md for the offline fallback)."""
    found = which('patchelf')
    if found:
        ver = run([found, '--version']).stdout.strip() or 'unknown version'
        return [found], f'system {found} ({ver})'
    tools = Path(repo) / '.state/tools' / f'patchelf-{PATCHELF_PIN}'
    exe = tools / 'bin/patchelf'
    if not exe.is_file():
        uv = which('uv') or str(Path.home() / '.local/bin/uv')
        req = Path(repo) / 'scripts/requirements-packaging.txt'
        if not Path(uv).is_file():
            die('patchelf not found. Install it (sudo apt install patchelf) or install uv for the pinned, hash-checked '
                'download (needs network); offline: pre-populate .state/tools, see docs/INSTALL.md')
        r = run([uv, 'pip', 'install', '--require-hashes', '--no-deps', '--python', '/usr/bin/python3',
                 '--target', str(tools), '-r', str(req)])
        if r.returncode or not exe.is_file():
            die(f'could not fetch patchelf=={PATCHELF_PIN} (network needed once; or `sudo apt install patchelf`): {r.stderr.strip()[-300:]}')
    ver = run([str(exe), '--version']).stdout.strip()
    return [str(exe)], f'pinned patchelf=={PATCHELF_PIN} (sha256-verified wheel) at {exe} ({ver})'


def check_prefix(prefix_arg, repo, home, protect=()):
    """Validate the destination physically (symlinks resolved) before anything is written."""
    if not home or not os.path.isabs(home):
        die('HOME is empty or not absolute; refusing to pick a default prefix')
    home_p = Path(home).resolve()
    if str(home_p) == '/':
        die('HOME resolves to /; refusing')
    if not prefix_arg or not os.path.isabs(prefix_arg):
        die('prefix must be an absolute path')
    if any(ord(c) < 32 for c in prefix_arg):
        die('prefix contains control characters')
    prefix = Path(os.path.abspath(prefix_arg)).resolve()
    repo = Path(repo).resolve()
    if str(prefix) == '/' or prefix == home_p or len(prefix.parts) < 3:
        die(f'refusing prefix {prefix}')
    for forbidden in [repo / '.deps', (repo / '.deps').resolve(), repo / '.state', repo / '.git', repo] + [Path(x).resolve() for x in protect]:
        if prefix == forbidden or forbidden in prefix.parents or prefix in forbidden.parents:
            die(f'prefix {prefix} may not live under, equal or contain {forbidden}')
    return prefix


def collect_plugins(qt_root, dev_ld, ldd_fn=None):
    """Select Qt plugins. Returns (plugins, skipped). A REQUIRED plugin with unresolved libraries is fatal."""
    ldd_fn = ldd_fn or ldd
    plugins, skipped = [], []
    for sub, names in PLUGIN_ALLOW.items():
        d = Path(qt_root) / 'plugins' / sub
        if not d.is_dir():
            continue
        for p in sorted(d.glob('*.so')):
            if names is not None and p.name not in names:
                continue
            _, missing = ldd_fn(p, dev_ld)
            if missing:
                if (sub, p.name) in REQUIRED_PLUGINS:
                    die(f'required plugin {sub}/{p.name} has unresolved libraries {missing}; install them '
                        f'(e.g. libxcb-cursor0) instead of building a prefix that cannot open a window')
                skipped.append((f'{sub}/{p.name}', missing))
                continue
            plugins.append((sub, p))
    return plugins, skipped


def require_platform_plugins(plugins):
    have = {(s, p.name) for s, p in plugins}
    for sub, name in REQUIRED_PLUGINS:
        if (sub, name) not in have:
            die(f'required plugin {sub}/{name} is missing from the Qt SDK; the prefix would not run on X11')


def is_elf(path):
    try:
        with open(path, 'rb') as f:
            return f.read(4) == b'\x7fELF' and not os.path.islink(path)
    except OSError:
        return False


def assemble(a, repo, deps, qt_root, sdk, sysroot, overlay, install, binary, fm_libs, prefix, stage):
    """Build the complete prefix in `stage` (a sibling directory of the final prefix)."""
    for d in ('bin', 'libexec', 'lib', 'plugins', 'share/win11-explorer-qt', 'share/icons/hicolor/scalable/apps', 'share/doc/win11-explorer-qt'):
        (stage / d).mkdir(parents=True)
    (stage / MARKER).write_text(MARKER_TEXT)

    dev_ld = ':'.join(str(p) for p in (overlay / 'lib', sdk / 'lib/x86_64-linux-gnu', sdk / 'lib',
                                        sysroot / 'usr/lib/x86_64-linux-gnu', qt_root / 'lib'))
    roots = [binary] + [p for p in fm_libs if p.is_file()]
    plugins, skipped = collect_plugins(qt_root, dev_ld)
    require_platform_plugins(plugins)
    syslibs = system_sonames()
    bundle, system_used, queue = {}, {}, list(roots) + [p for _, p in plugins]
    seen = set()
    while queue:
        item = queue.pop()
        if item in seen:
            continue
        seen.add(item)
        found, missing = ldd(item, dev_ld)
        if missing:
            die(f'{item}: unresolved even with the dev SDK: {missing}')
        for soname, resolved in found.items():
            rp = Path(resolved).resolve()
            inside = lambda root: root == rp or root in rp.parents
            if inside(qt_root) or inside(overlay) or inside(sdk):
                bundle.setdefault(soname, rp)
                queue.append(rp)
            elif inside(sysroot):
                if soname in syslibs:
                    system_used[soname] = f'system (sysroot has {rp.name})'
                else:
                    bundle.setdefault(soname, rp)
                    queue.append(rp)
            elif str(rp).startswith(SYSTEM_LIB_DIRS):
                system_used[soname] = str(rp)
            else:
                die(f'{item}: {soname} resolves to {rp}, outside the SDK and the system library directories')
    lib = stage / 'lib'
    for soname, src in sorted(bundle.items()):
        if soname.startswith('libfm-qt6'):
            continue
        shutil.copy2(src, lib / soname)
    for p in fm_libs:  # keep libfm-qt6.so.17 -> 17.x.y as in the overlay
        if p.is_symlink():
            os.symlink(os.readlink(p), lib / p.name)
        else:
            shutil.copy2(p, lib / p.name)
    shutil.copy2(binary, stage / 'libexec/win11-explorer-qt')
    for sub, p in plugins:
        (stage / 'plugins' / sub).mkdir(parents=True, exist_ok=True)
        shutil.copy2(p, stage / 'plugins' / sub / p.name)
    (stage / 'libexec/qt.conf').write_text('[Paths]\nPrefix = ..\nPlugins = plugins\nLibraries = lib\n')
    # data: translations etc. from the install tree, but not the dev-only guard file
    data = install / 'share/win11-explorer-qt'
    if data.is_dir():
        shutil.copytree(data, stage / 'share/win11-explorer-qt', dirs_exist_ok=True,
                        ignore=shutil.ignore_patterns('private-libfm-runtime.txt'))
    shutil.copy2(repo / 'packaging/win11-explorer-qt.sh', stage / 'bin/win11-explorer-qt')
    os.chmod(stage / 'bin/win11-explorer-qt', 0o755)
    shutil.copy2(repo / 'packaging/win11-explorer-qt.desktop.in', stage / 'share/win11-explorer-qt/win11-explorer-qt.desktop.in')
    shutil.copy2(repo / 'packaging/icons/win11-explorer-qt.svg', stage / 'share/icons/hicolor/scalable/apps/win11-explorer-qt.svg')
    pkg_licenses.install_license_docs(stage / 'share/doc/win11-explorer-qt', repo, qt_root,
                                      menu_cache_copyright=sysroot / 'usr/share/doc/libmenu-cache3/copyright')
    absent = pkg_licenses.missing_docs(stage / 'share/doc/win11-explorer-qt')
    if absent:
        die(f'license documents missing from the prefix: {absent}')

    # Debug info (RelWithDebInfo): keep it in separate files, ship stripped ELFs.
    (stage / 'lib/debug').mkdir()
    for target in [stage / 'libexec/win11-explorer-qt'] + [p for p in lib.glob('libfm-qt6.so.*') if is_elf(p)]:
        dbg = stage / 'lib/debug' / (target.name + '.debug')
        for cmd in (['objcopy', '--only-keep-debug', str(target), str(dbg)],
                    ['strip', '--strip-unneeded', str(target)],
                    ['objcopy', f'--add-gnu-debuglink={dbg}', str(target)]):
            r = run(cmd)
            if r.returncode:
                die(f'{cmd[0]} failed on {target}: {r.stderr}')

    # RPATHs (DT_RPATH via --force-rpath): relative only. ($ORIGIN-based; transitive deps need each lib's own RPATH.)
    pe, pe_desc = patchelf_cmd(repo)
    def setrpath(path, value):
        r = run(pe + ['--force-rpath', '--set-rpath', value, str(path)])  # DT_RPATH wins over an inherited LD_LIBRARY_PATH
        if r.returncode:
            die(f'patchelf failed on {path}: {r.stderr}')
    setrpath(stage / 'libexec/win11-explorer-qt', '$ORIGIN/../lib')
    for p in sorted(lib.iterdir()):
        if is_elf(p):
            setrpath(p, '$ORIGIN')
    for p in sorted((stage / 'plugins').rglob('*.so')):
        setrpath(p, '$ORIGIN/../../lib')

    report = ['# Bundled in lib/ (copied from the private SDK/overlay)']
    report += [f'{k} <- {v.name}' for k, v in sorted(bundle.items())]
    report += ['', '# Resolved from the host system (not bundled)']
    report += [f'{k} -> {v}' for k, v in sorted(system_used.items())]
    report += ['', '# Plugins']
    report += [f'{sub}/{p.name}' for sub, p in plugins]
    report += ['', '# Plugins skipped (optional, unresolved libraries on the build host)']
    report += [f'{n}: {m}' for n, m in skipped] or ['(none)']
    report += ['', '# Build tool', f'patchelf: {pe_desc}']
    (stage / 'share/win11-explorer-qt/BUNDLED.txt').write_text('\n'.join(report) + '\n')

    # Self-check: relative RPATHs, nothing resolving into the dev tree, xcb present and resolvable.
    errors = []
    if not (stage / 'plugins/platforms/libqxcb.so').is_file():
        errors.append('plugins/platforms/libqxcb.so is missing')
    for p in [stage / 'libexec/win11-explorer-qt'] + sorted(stage.rglob('*.so*')):
        if not is_elf(p):
            continue
        dyn = run(['readelf', '-d', str(p)]).stdout
        for m in re.finditer(r'\((?:RUNPATH|RPATH)\)[^\[]*\[([^\]]*)\]', dyn):
            for entry in m.group(1).split(':'):
                if not entry.startswith('$ORIGIN'):
                    errors.append(f'{p}: absolute RPATH {entry}')
        found, missing = ldd(p, None)
        errors += [f'{p}: unresolved {m}' for m in missing]
        for soname, resolved in found.items():
            rp = os.path.realpath(resolved)
            if not (rp.startswith(SYSTEM_LIB_DIRS) or rp.startswith(str(stage.resolve()) + '/')):
                errors.append(f'{p}: {soname} -> {resolved} (neither the prefix nor a system library directory)')
    if errors:
        print('\n'.join(errors[:40]), file=sys.stderr)
        die(f'{len(errors)} relocation problems in {prefix}')
    size = sum(f.stat().st_size for f in stage.rglob('*') if f.is_file())
    return f'({size / 1048576:.1f} MiB, {len(bundle)} bundled libs, {len(plugins)} plugins)'


def has_marker(path):
    marker = Path(path) / MARKER
    try:
        return marker.is_file() and not marker.is_symlink() and marker.read_text().startswith(MARKER_TEXT.strip())
    except OSError:
        return False


def remove_tree(path):
    """rmtree that never follows or chokes on a symlink at the top."""
    if os.path.islink(path):
        os.unlink(path)
    else:
        shutil.rmtree(path)


def check_prev(prev):
    """`.NAME.prev` may only be deleted/restored if it is a real directory carrying our marker."""
    if not os.path.lexists(prev):
        return
    if os.path.islink(prev) or not os.path.isdir(prev) or not has_marker(prev):
        die(f'{prev} exists but is not a prefix created by this script (symlink or no valid {MARKER}); '
            'refusing to delete it; move it away and retry')


def pid_alive(pid):
    try:
        os.kill(pid, 0)
    except ProcessLookupError:
        return False
    except PermissionError:
        return True
    return True


STAGE_DIRS = {'bin', 'libexec', 'lib', 'plugins', 'share'}


def clean_orphans(prefix, prev):
    """Housekeeping for interrupted runs: remove `.NAME.new.<pid>` staging directories whose pid is dead
    (never symlinks, never a directory that is not ours) and put a lone `.NAME.prev` (kill between the two
    renames) back in place."""
    prefix, prev = Path(prefix), Path(prev)
    pat = re.compile(re.escape(f'.{prefix.name}.new.') + r'(\d+)$')
    for p in sorted(prefix.parent.iterdir()):
        m = pat.match(p.name)
        if not m or p.is_symlink() or not p.is_dir() or pid_alive(int(m.group(1))):
            continue
        if has_marker(p) or not (p / MARKER).exists() and {c.name for c in p.iterdir()} <= STAGE_DIRS:
            shutil.rmtree(p, ignore_errors=True)
    if os.path.lexists(prev) and not os.path.lexists(prefix) and not prev.is_symlink() and prev.is_dir() and has_marker(prev):
        os.rename(prev, prefix)
        print(f'note: restored {prefix} from {prev} (an earlier update was interrupted)', file=sys.stderr)


def publish(prefix, stage, prev):
    """Swap `stage` into place. The old prefix stays as `.NAME.prev` until the new one is in place;
    any failure cleans the stage and puts the old prefix back."""
    prefix, stage, prev = Path(prefix), Path(stage), Path(prev)
    moved = False
    try:
        if os.path.lexists(prefix):
            check_prev(prev)
            if os.path.lexists(prev):
                shutil.rmtree(prev)
            os.rename(prefix, prev)
            moved = True
        os.rename(stage, prefix)
    except BaseException:
        if moved and not os.path.lexists(prefix) and prev.is_dir():
            os.rename(prev, prefix)
        shutil.rmtree(stage, ignore_errors=True)
        raise
    shutil.rmtree(prev, ignore_errors=True)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--repo', required=True)
    ap.add_argument('--prefix', required=True)
    ap.add_argument('--install-dir', required=True)
    ap.add_argument('--overlay', required=True)
    a = ap.parse_args()
    repo = Path(a.repo).resolve()
    deps = repo / '.deps'
    qt_root = (deps / 'Qt/6.8.3/gcc_64').resolve()
    sdk = (deps / 'prefix').resolve()
    sysroot = (deps / 'sysroot').resolve()
    overlay = Path(a.overlay).resolve()
    install = Path(a.install_dir).resolve()
    prefix = check_prefix(a.prefix, repo, os.environ.get('HOME', ''), protect=[overlay, install])
    binary = install / 'bin/win11-explorer-qt'
    if not binary.is_file():
        die(f'missing built binary {binary}; run scripts/build.sh first')
    fm_libs = sorted(overlay.glob('lib/libfm-qt6.so*'))
    if not fm_libs:
        die(f'missing private libfm-qt6 in {overlay}/lib')
    if prefix.exists():
        marker = prefix / MARKER
        if not marker.is_file() or not marker.read_text().startswith(MARKER_TEXT.strip()):
            die(f'{prefix} exists and is not a prefix created by this script (no valid {MARKER}); refusing to touch it')
    prefix.parent.mkdir(parents=True, exist_ok=True)
    prev = prefix.parent / f'.{prefix.name}.prev'
    clean_orphans(prefix, prev)
    check_prev(prev)  # fail before the ~1 s assembly, not after it
    stage = prefix.parent / f'.{prefix.name}.new.{os.getpid()}'
    if stage.is_symlink() or stage.exists():
        remove_tree(stage)
    try:
        summary = assemble(a, repo, deps, qt_root, sdk, sysroot, overlay, install, binary, fm_libs, prefix, stage)
    except BaseException:
        shutil.rmtree(stage, ignore_errors=True)  # the previous prefix was never touched
        raise
    publish(prefix, stage, prev)
    print(f'prefix ready: {prefix} {summary}')


if __name__ == '__main__':
    main()
