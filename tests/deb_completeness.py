#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Completeness of a .deb's Depends, derived from the ELF files it ships (not from a list).

For every ELF under /opt/win11-explorer-qt: DT_NEEDED sonames that the package does not carry
itself -> the file the host resolves them to (ldconfig -p) -> owning package (dpkg -S) -> that
package must be declared in Depends or be reachable from it (apt-cache depends --recurse).
The NEEDED closure continues through the system libraries, so a library that is only pulled in
by another system library (and whose package does not declare it) is reported as well.

CLI: deb_completeness.py FILE.deb   (read-only; nothing is installed)
"""
import os
import re
import subprocess
import sys
import tempfile
from pathlib import Path

DEPENDS_ONLY = ['--no-recommends', '--no-suggests', '--no-conflicts', '--no-breaks', '--no-replaces', '--no-enhances']


def run(cmd, **kw):
    # LC_ALL=C: readelf/dpkg/apt-cache output is translated (pt_BR here) and the parsers match English.
    env = dict(os.environ, LC_ALL='C', LANGUAGE='C')
    return subprocess.run(cmd, text=True, capture_output=True, env=env, **kw)


def is_elf(path):
    try:
        with open(path, 'rb') as f:
            return f.read(4) == b'\x7fELF' and not os.path.islink(path)
    except OSError:
        return False


def needed(path):
    out = run(['readelf', '-d', str(path)]).stdout
    return re.findall(r'\(NEEDED\)\s+Shared library: \[([^\]]+)\]', out)


def ldconfig_map():
    out = run(['/sbin/ldconfig', '-p']).stdout
    m = {}
    for line in out.splitlines()[1:]:
        mm = re.match(r'\s*(\S+) \(([^)]*)\) => (\S+)', line)
        if mm and ('x86-64' in mm.group(2) or 'libc6' == mm.group(2)):
            m.setdefault(mm.group(1), mm.group(3))
    return m


def owner_package(path):
    """dpkg -S for the path and its physical realpath (usrmerge: /lib -> /usr/lib)."""
    for cand in dict.fromkeys([str(path), os.path.realpath(path)]):
        r = run(['dpkg', '-S', cand])
        if r.returncode == 0 and r.stdout.strip():
            first = r.stdout.splitlines()[0]
            return first.split(':', 1)[0] if not first.startswith('diversion') else None
    return None


def declared_names(depends_field):
    """Package names of a Depends field; the first alternative of `a | b` and all of them count."""
    names = []
    for item in depends_field.split(','):
        for alt in item.split('|'):
            alt = alt.strip()
            if alt:
                names.append(re.split(r'[ (:]', alt)[0])
    return names


def apt_closure(names):
    """Names reachable through Depends/PreDepends (apt-cache depends --recurse)."""
    out = run(['apt-cache', 'depends', '--recurse', *DEPENDS_ONLY, *names]).stdout
    return {l.strip() for l in out.splitlines() if l and not l[0].isspace()} | set(names)


def missing_packages(needed_by_package, declared, closure_fn=apt_closure):
    """{package: [sonames]} for owners that are neither declared nor reachable from Depends."""
    reach = closure_fn(declared)
    return {p: s for p, s in sorted(needed_by_package.items()) if p not in reach}


# Qt skips these plugins silently when a library is missing, so their libraries may be Recommends.
OPTIONAL_PLUGIN_DIRS = {'platformthemes', 'imageformats', 'tls'}
# soname -> package for libraries that are not installed on this host (nothing for dpkg -S to find).
# The bundled copy of libmenu-cache.so.3 is deliberately dropped from the .deb.
NOT_INSTALLED_HERE = {'libmenu-cache.so.3': 'libmenu-cache3'}


def elf_files(root):
    for p in sorted(Path(root).rglob('*')):
        if p.is_file() and not p.is_symlink() and (p.suffix == '.so' or '.so.' in p.name or p.parent.name in ('libexec', 'bin')) and is_elf(p):
            yield p


def is_optional(tree, path):
    rel = Path(path).relative_to(tree).parts
    return rel[0] == 'plugins' and len(rel) > 2 and rel[1] in OPTIONAL_PLUGIN_DIRS


def walk_needed(roots, own, ldmap, owner, skip=frozenset()):
    """NEEDED closure from `roots` through system libraries -> ({package: set(sonames)}, unresolved, seen)."""
    todo = [s for p in roots for s in needed(p)]
    seen, by_pkg, unresolved = set(), {}, []
    while todo:
        so = todo.pop()
        if so in seen or so in own or so in skip:
            continue
        seen.add(so)
        path = ldmap.get(so)
        if not path:
            if so in NOT_INSTALLED_HERE:
                by_pkg.setdefault(NOT_INSTALLED_HERE[so], set()).add(so)
            else:
                unresolved.append(so)
            continue
        pkg = owner(path)
        if pkg is None:
            unresolved.append(f'{so} (no dpkg owner for {path})')
            continue
        by_pkg.setdefault(pkg, set()).add(so)
        todo.extend(needed(path))
    return by_pkg, unresolved, seen


def system_needed_by_package(tree, ldmap=None, owner=owner_package):
    """(hard {package: sonames}, optional {package: sonames}, unresolved): what the packaged ELFs
    need from the host. `optional` = only reached through plugins that Qt skips when they cannot load."""
    ldmap = ldmap if ldmap is not None else ldconfig_map()
    tree = Path(tree)
    own = {p.name for p in tree.rglob('*.so*')}
    elfs = list(elf_files(tree))
    hard_roots = [p for p in elfs if not is_optional(tree, p)]
    opt_roots = [p for p in elfs if is_optional(tree, p)]
    hard, un1, hard_seen = walk_needed(hard_roots, own, ldmap, owner)
    opt, un2, _ = walk_needed(opt_roots, own, ldmap, owner, skip=hard_seen)
    fmt = lambda d: {k: sorted(v) for k, v in sorted(d.items())}
    return fmt(hard), fmt(opt), sorted(set(un1 + un2))


def check_deb(deb):
    """{'hard': {pkg: sonames}, 'optional': {...}, 'unresolved': [...], 'depends': [...]} of what is NOT covered."""
    depends = run(['dpkg-deb', '-f', str(deb), 'Depends']).stdout.strip()
    recommends = run(['dpkg-deb', '-f', str(deb), 'Recommends']).stdout.strip()
    dnames, rnames = declared_names(depends), declared_names(recommends)
    with tempfile.TemporaryDirectory() as tmp:
        r = run(['dpkg-deb', '-x', str(deb), tmp])
        if r.returncode:
            raise RuntimeError(r.stderr)
        hard, opt, unresolved = system_needed_by_package(Path(tmp) / 'opt/win11-explorer-qt')
    return {'hard': missing_packages(hard, dnames),
            'optional': missing_packages(opt, dnames + rnames),
            'unresolved': unresolved, 'depends': dnames, 'recommends': rnames}


if __name__ == '__main__':
    res = check_deb(sys.argv[1])
    for k, v in res.items():
        print(f'{k}: {v}')
    sys.exit(1 if res['hard'] or res['optional'] or res['unresolved'] else 0)
