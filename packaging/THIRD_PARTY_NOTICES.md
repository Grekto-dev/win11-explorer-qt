# Third-party components redistributed with Win11 Explorer Qt

This prefix / `.deb` is a binary distribution. Besides the application itself it carries the
components below. The exact list for a given build is in `share/win11-explorer-qt/BUNDLED.txt`.

| Component | Version | License | Where |
|---|---|---|---|
| Win11 Explorer Qt (fork of PCManFM-Qt) | 2.4.1-based | GPL-2.0-or-later (`LICENSE`) | `libexec/win11-explorer-qt` |
| libfm-qt (patched, private overlay) | 2.4.0 + project patches | LGPL-2.1-or-later (`LICENSE.libfm-qt.LGPL-2.1`) | `lib/libfm-qt6.so.17` |
| Qt 6.8.3: Core, DBus, Gui, Network, Svg, Widgets, XcbQpa + platform/imageformat/iconengine/theme plugins | 6.8.3 (official Qt binaries; only their RPATH is rewritten, see below) | LGPL-3.0-only (`LICENSE.Qt-LGPL-3.0`); also available under GPL-3.0 (`LICENSE.Qt-GPL-3.0`) | `lib/libQt6*.so.6`, `plugins/` |
| ICU (libicuuc, libicui18n, libicudata) | 73.2 (as shipped inside the Qt binaries) | Unicode/ICU license (MIT style) with the BSD notices for the Lao/Burmese dictionaries and Google double-conversion; the complete upstream `icu4c/LICENSE` of release-73-2 is `LICENSE.ICU` | `lib/libicu*.so.73` |
| libmenu-cache | 1.1.0 | GPL-2.0-or-later / LGPL-2.1-or-later (see `copyright.libmenu-cache3`) | `lib/libmenu-cache.so.3` in the user prefix only; the `.deb` depends on the system `libmenu-cache3` instead |

Everything else (glibc, libstdc++, GLib, X11/xcb, GL, fontconfig, freetype, GTK3 ...) comes from the
host distribution and is not redistributed.

## License combination

The application is GPL-2.0-or-later. Qt is LGPL-3.0-only (or GPL-3.0); the combination is distributed
under the "or later" option, i.e. GPL-3.0 for the combined binary. libfm-qt is LGPL-2.1-or-later.

## Source code (LGPL/GPL offer)

* Qt 6.8.3 corresponding source: https://download.qt.io/archive/qt/6.8/6.8.3/single/qt-everywhere-src-6.8.3.tar.xz
  (also https://code.qt.io/cgit/qt/qtbase.git, tag `v6.8.3`). The shipped libraries are the official binaries; the only binary change is the dynamic-section
  RPATH (`$ORIGIN`-relative), rewritten with `patchelf` so that they load from the private prefix. Qt itself bundles
  third-party code (e.g. PCRE2, double-conversion, md4c) under its own licenses; see the Qt source above.
* ICU 73.2 source: https://github.com/unicode-org/icu/releases/tag/release-73-2
* libmenu-cache 1.1.0 source: https://github.com/lxde/menu-cache (Ubuntu source package `menu-cache`).
* libfm-qt 2.4.0 upstream: https://github.com/lxqt/libfm-qt; the project patches that change it live in this
  repository under `patches/libfm-qt/`.
* Application source: https://github.com/Grekto-dev/win11-explorer-qt (written offer: the same source is
  available on request from the distributor of this binary for three years after the distribution).

## Replacing / relinking the LGPL libraries

All LGPL libraries are ordinary shared objects (`lib/*.so*`, `plugins/**/*.so`) found through a relative
DT_RPATH (`$ORIGIN`) and `qt.conf`. To use your own build of Qt, ICU or libfm-qt, replace the file with the
same SONAME in `lib/` (ABI-compatible build required) and start the program normally: no relinking of the
application is necessary because it links to them dynamically. `libexec/win11-explorer-qt` can be re-linked
from the source above against another libfm-qt/Qt.
