#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Assemble a relocatable prefix from an already built tree (see make-portable-prefix.sh).

Layout:  bin/ (launcher)  libexec/ (ELF + qt.conf)  lib/ (private libfm-qt, Qt, ICU,
libs absent from the host)  plugins/  share/.  Only the prefix is written."""
import argparse, os, re, shutil, subprocess, sys
from pathlib import Path

MARKER = '.win11-explorer-qt-prefix'
PLUGIN_ALLOW = {
    'platforms': ['libqxcb.so', 'libqoffscreen.so', 'libqminimal.so'],
    'xcbglintegrations': None, 'platformthemes': None, 'imageformats': None,
    'iconengines': None, 'tls': None, 'platforminputcontexts': None, 'styles': None,
}


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


def patchelf_cmd():
    if shutil.which('patchelf'):
        return ['patchelf']
    return ['uv', 'tool', 'run', '--from', 'patchelf', 'patchelf']


def is_elf(path):
    try:
        with open(path, 'rb') as f:
            return f.read(4) == b'\x7fELF' and not os.path.islink(path)
    except OSError:
        return False


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
    prefix = Path(a.prefix)
    if not prefix.is_absolute():
        die('prefix must be absolute')
    prefix = Path(os.path.abspath(prefix))
    home = Path(os.environ.get('HOME', '/nonexistent')).resolve()
    if str(prefix) in ('/', str(home)) or len(prefix.parts) < 3:
        die(f'refusing prefix {prefix}')
    for forbidden in (deps, repo / '.state', repo / '.git'):
        if prefix == forbidden or forbidden in prefix.parents:
            die(f'prefix may not live under {forbidden}')
    binary = install / 'bin/win11-explorer-qt'
    if not binary.is_file():
        die(f'missing built binary {binary}; run scripts/build.sh first')
    fm_libs = sorted(overlay.glob('lib/libfm-qt6.so*'))
    if not fm_libs:
        die(f'missing private libfm-qt6 in {overlay}/lib')
    if prefix.exists():
        if not (prefix / MARKER).is_file():
            die(f'{prefix} exists and is not a prefix created by this script (no {MARKER}); refusing to touch it')
        shutil.rmtree(prefix)
    for d in ('bin', 'libexec', 'lib', 'plugins', 'share/win11-explorer-qt', 'share/icons/hicolor/scalable/apps', 'share/doc/win11-explorer-qt'):
        (prefix / d).mkdir(parents=True)
    (prefix / MARKER).write_text('created by scripts/make-portable-prefix.sh\n')

    dev_ld = ':'.join(str(p) for p in (overlay / 'lib', sdk / 'lib/x86_64-linux-gnu', sdk / 'lib',
                                        sysroot / 'usr/lib/x86_64-linux-gnu', qt_root / 'lib'))
    roots = [binary] + [p for p in fm_libs if p.is_file()]
    plugins = []
    for sub, names in PLUGIN_ALLOW.items():
        d = qt_root / 'plugins' / sub
        if not d.is_dir():
            continue
        for p in sorted(d.glob('*.so')):
            if names is not None and p.name not in names:
                continue
            _, missing = ldd(p, dev_ld)
            if missing:
                print(f'skip plugin {sub}/{p.name}: unresolved {missing}')
                continue
            plugins.append((sub, p))
    syslibs = system_sonames()
    qt_lib, ov_lib = qt_root / 'lib', overlay / 'lib'
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
            else:
                system_used[soname] = str(rp)
    lib = prefix / 'lib'
    for soname, src in sorted(bundle.items()):
        if soname.startswith('libfm-qt6'):
            continue
        shutil.copy2(src, lib / soname)
    for p in fm_libs:  # keep libfm-qt6.so.17 -> 17.x.y as in the overlay
        if p.is_symlink():
            os.symlink(os.readlink(p), lib / p.name)
        else:
            shutil.copy2(p, lib / p.name)
    shutil.copy2(binary, prefix / 'libexec/win11-explorer-qt')
    for sub, p in plugins:
        (prefix / 'plugins' / sub).mkdir(parents=True, exist_ok=True)
        shutil.copy2(p, prefix / 'plugins' / sub / p.name)
    (prefix / 'libexec/qt.conf').write_text('[Paths]\nPrefix = ..\nPlugins = plugins\nLibraries = lib\n')
    # data: translations etc. from the install tree, but not the dev-only guard file
    data = install / 'share/win11-explorer-qt'
    if data.is_dir():
        shutil.copytree(data, prefix / 'share/win11-explorer-qt', dirs_exist_ok=True,
                        ignore=shutil.ignore_patterns('private-libfm-runtime.txt'))
    shutil.copy2(repo / 'packaging/win11-explorer-qt.sh', prefix / 'bin/win11-explorer-qt')
    os.chmod(prefix / 'bin/win11-explorer-qt', 0o755)
    shutil.copy2(repo / 'packaging/win11-explorer-qt.desktop.in', prefix / 'share/win11-explorer-qt/win11-explorer-qt.desktop.in')
    shutil.copy2(repo / 'packaging/icons/win11-explorer-qt.svg', prefix / 'share/icons/hicolor/scalable/apps/win11-explorer-qt.svg')
    for doc in ('LICENSE', 'AUTHORS', 'README.upstream.md'):
        if (repo / doc).is_file():
            shutil.copy2(repo / doc, prefix / 'share/doc/win11-explorer-qt' / doc)
    lic = repo / 'patches/libfm-qt/LICENSE.LGPL-2.1'
    if lic.is_file():
        shutil.copy2(lic, prefix / 'share/doc/win11-explorer-qt/LICENSE.libfm-qt.LGPL-2.1')

    # Debug info (RelWithDebInfo): keep it in separate files, ship stripped ELFs.
    (prefix / 'lib/debug').mkdir()
    for target in [prefix / 'libexec/win11-explorer-qt'] + [p for p in lib.glob('libfm-qt6.so.*') if is_elf(p)]:
        dbg = prefix / 'lib/debug' / (target.name + '.debug')
        for cmd in (['objcopy', '--only-keep-debug', str(target), str(dbg)],
                    ['strip', '--strip-unneeded', str(target)],
                    ['objcopy', f'--add-gnu-debuglink={dbg}', str(target)]):
            r = run(cmd)
            if r.returncode:
                die(f'{cmd[0]} failed on {target}: {r.stderr}')

    # RUNPATHs: relative only. ($ORIGIN-based; transitive deps need each lib's own RUNPATH.)
    pe = patchelf_cmd()
    def setrpath(path, value):
        r = run(pe + ['--set-rpath', value, str(path)])
        if r.returncode:
            die(f'patchelf failed on {path}: {r.stderr}')
    setrpath(prefix / 'libexec/win11-explorer-qt', '$ORIGIN/../lib')
    for p in sorted(lib.iterdir()):
        if is_elf(p):
            setrpath(p, '$ORIGIN')
    for p in sorted((prefix / 'plugins').rglob('*.so')):
        setrpath(p, '$ORIGIN/../../lib')

    report = ['# Bundled in lib/ (copied from the private SDK/overlay)']
    report += [f'{k} <- {v.name}' for k, v in sorted(bundle.items())]
    report += ['', '# Resolved from the host system (not bundled)']
    report += [f'{k} -> {v}' for k, v in sorted(system_used.items())]
    report += ['', '# Plugins']
    report += [f'{sub}/{p.name}' for sub, p in plugins]
    (prefix / 'share/win11-explorer-qt/BUNDLED.txt').write_text('\n'.join(report) + '\n')

    # Self-check: relative RUNPATHs and nothing resolving into the dev tree.
    errors = []
    for p in [prefix / 'libexec/win11-explorer-qt'] + sorted(prefix.rglob('*.so*')):
        if not is_elf(p):
            continue
        dyn = run(['readelf', '-d', str(p)]).stdout
        for m in re.finditer(r'\((?:RUNPATH|RPATH)\)[^\[]*\[([^\]]*)\]', dyn):
            for entry in m.group(1).split(':'):
                if not entry.startswith('$ORIGIN'):
                    errors.append(f'{p}: absolute RUNPATH {entry}')
        found, missing = ldd(p, None)
        errors += [f'{p}: unresolved {m}' for m in missing]
        for soname, resolved in found.items():
            if any(x in resolved for x in ('/.deps/', '/.state/', 'sysroot')):
                errors.append(f'{p}: {soname} -> {resolved}')
    if errors:
        print('\n'.join(errors[:40]), file=sys.stderr)
        die(f'{len(errors)} relocation problems in {prefix}')
    size = sum(f.stat().st_size for f in prefix.rglob('*') if f.is_file())
    print(f'prefix ready: {prefix} ({size / 1048576:.1f} MiB, {len(bundle)} bundled libs, {len(plugins)} plugins)')


if __name__ == '__main__':
    main()
