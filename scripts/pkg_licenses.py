#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""License documents that must accompany every redistributed prefix / .deb."""
import shutil
import sys
from pathlib import Path

REQUIRED_DOCS = ['LICENSE', 'LICENSE.libfm-qt.LGPL-2.1', 'LICENSE.Qt-LGPL-3.0', 'LICENSE.Qt-GPL-3.0',
                 'LICENSE.ICU', 'THIRD_PARTY_NOTICES.md']
COMMON = Path('/usr/share/common-licenses')


def die(msg):
    print(f'error: {msg}', file=sys.stderr)
    sys.exit(1)


def missing_docs(dest):
    dest = Path(dest)
    return [n for n in REQUIRED_DOCS if not (dest / n).is_file() or (dest / n).stat().st_size == 0]


def _first(*candidates):
    for c in candidates:
        if c and Path(c).is_file() and Path(c).stat().st_size > 200:
            return Path(c)
    return None


def install_license_docs(dest, repo, qt_root, menu_cache_copyright=None, common_licenses=COMMON):
    """Copy license texts into dest (share/doc/win11-explorer-qt). Missing source = hard error."""
    dest, repo, qt_root, common = Path(dest), Path(repo), Path(qt_root), Path(common_licenses)
    dest.mkdir(parents=True, exist_ok=True)
    sources = {
        'LICENSE': _first(repo / 'LICENSE'),
        'LICENSE.libfm-qt.LGPL-2.1': _first(repo / 'patches/libfm-qt/LICENSE.LGPL-2.1'),
        'LICENSE.Qt-LGPL-3.0': _first(qt_root / 'LICENSES/LGPL-3.0-only.txt', qt_root / 'LICENSE.LGPL3', common / 'LGPL-3'),
        'LICENSE.Qt-GPL-3.0': _first(qt_root / 'LICENSES/GPL-3.0-only.txt', qt_root / 'LICENSE.GPL3', common / 'GPL-3'),
        'LICENSE.ICU': _first(repo / 'packaging/licenses/LICENSE.ICU'),
        'THIRD_PARTY_NOTICES.md': _first(repo / 'packaging/THIRD_PARTY_NOTICES.md'),
    }
    for name, src in sources.items():
        if src is None:
            die(f'cannot find the source of required license document {name} (looked in the repo, Qt SDK, {common})')
        shutil.copy2(src, dest / name)
    if menu_cache_copyright and Path(menu_cache_copyright).is_file():
        shutil.copy2(menu_cache_copyright, dest / 'copyright.libmenu-cache3')
    return dest
