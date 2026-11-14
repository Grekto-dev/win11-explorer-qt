#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-2.0-or-later
# Build a .deb from a prefix made by make-portable-prefix.sh. Nothing is installed;
# the package carries /opt/win11-explorer-qt, a /usr/bin link, desktop entry, icon, the
# license documents and a Debian copyright file. Target: Ubuntu 24.04 / Linux Mint 22.
set -euo pipefail
repo="$(cd -- "$(dirname -- "$(readlink -f -- "${BASH_SOURCE[0]}")")/.." && pwd)"
prefix="" out="$repo/.state/deb" version="2.4.1+win11explorer1" strip_debug=0
while (($#)); do
    case "$1" in
        --prefix) (($# >= 2)) || { echo "--prefix needs a directory" >&2; exit 2; }; prefix="$2"; shift 2 ;;
        --out) (($# >= 2)) || { echo "--out needs a directory" >&2; exit 2; }; out="$2"; shift 2 ;;
        --version) (($# >= 2)) || { echo "--version needs a value" >&2; exit 2; }; version="$2"; shift 2 ;;
        --without-debug) strip_debug=1; shift ;;
        -h|--help) printf 'Usage: %s --prefix PREFIX [--out DIR] [--version V] [--without-debug]\n' "$0"; exit 0 ;;
        *) printf 'Unknown option: %s\n' "$1" >&2; exit 2 ;;
    esac
done
[[ "$version" =~ ^[0-9][A-Za-z0-9.+~-]*$ ]] || { printf 'Invalid Debian version: %q\n' "$version" >&2; exit 2; }
[[ -x "$prefix/bin/win11-explorer-qt" && -f "$prefix/share/win11-explorer-qt/win11-explorer-qt.desktop.in" ]] || {
    printf 'Not a prefix from make-portable-prefix.sh: %s\n' "$prefix" >&2; exit 1; }
docs="$prefix/share/doc/win11-explorer-qt"
for d in LICENSE LICENSE.libfm-qt.LGPL-2.1 LICENSE.Qt-LGPL-3.0 LICENSE.Qt-GPL-3.0 LICENSE.ICU THIRD_PARTY_NOTICES.md; do
    [[ -s "$docs/$d" ]] || { printf 'The prefix lacks the license document %s (redistributing Qt/ICU without it is not allowed); rebuild the prefix\n' "$d" >&2; exit 1; }
done
# The prefix may be old or hand-edited: re-impose the pinned ICU text (same constant the assembler checks).
icu_sha="$(/usr/bin/python3 -c 'import sys; sys.path.insert(0, sys.argv[1]); import pkg_licenses; print(pkg_licenses.ICU_LICENSE_SHA256)' "$repo/scripts")" \
    || { echo "cannot read pkg_licenses.ICU_LICENSE_SHA256" >&2; exit 1; }
[[ "$(sha256sum -- "$docs/LICENSE.ICU" | cut -d' ' -f1)" == "$icu_sha" ]] || {
    printf 'LICENSE.ICU in the prefix is not the pinned ICU 73.2 text (sha256 %s); rebuild the prefix with make-portable-prefix.sh\n' "$icu_sha" >&2; exit 1; }
reqs="$(/usr/bin/python3 "$repo/scripts/elf_requirements.py" "$prefix")" || { echo "cannot derive minimum glibc/libstdc++ from the ELF files" >&2; exit 1; }
glibc_min="$(sed -n 's/.*glibc=\([0-9.]*\).*/\1/p' <<<"$reqs")"
gcc_min="$(sed -n 's/.*libstdcxx_pkg=\([0-9.]*\)$/\1/p' <<<"$reqs")"
libc_dep="libc6${glibc_min:+ (>= $glibc_min)}"
cxx_dep="libstdc++6${gcc_min:+ (>= $gcc_min)}"
command -v dpkg-deb >/dev/null || { echo "dpkg-deb not found" >&2; exit 1; }
stage="$(mktemp -d)"
trap 'rm -rf -- "$stage"' EXIT
dest=/opt/win11-explorer-qt
mkdir -p "$stage/DEBIAN" "$stage/opt" "$stage/usr/bin" "$stage/usr/share/applications" \
    "$stage/usr/share/icons/hicolor/scalable/apps" "$out"
cp -a -- "$prefix" "$stage/opt/win11-explorer-qt"
rm -f -- "$stage/opt/win11-explorer-qt/.win11-explorer-qt-prefix"
((strip_debug)) && rm -rf -- "$stage/opt/win11-explorer-qt/lib/debug"
# libmenu-cache3 (Depends) provides the library and a matching menu-cached path; the bundled copy hardcodes /usr/lib/menu-cache.
rm -f -- "$stage/opt/win11-explorer-qt/lib/libmenu-cache.so.3"
ln -s "$dest/bin/win11-explorer-qt" "$stage/usr/bin/win11-explorer-qt"
/usr/bin/python3 "$repo/scripts/render_desktop.py" "$prefix/share/win11-explorer-qt/win11-explorer-qt.desktop.in" "$dest/bin" \
    > "$stage/usr/share/applications/win11-explorer-qt.desktop"
mkdir -p "$stage/usr/share/doc/win11-explorer-qt"
cp -- "$docs"/* "$stage/usr/share/doc/win11-explorer-qt/"
rm -f -- "$stage/usr/share/doc/win11-explorer-qt/copyright.libmenu-cache3"  # that library is not shipped in the .deb
rm -rf -- "$stage/opt/win11-explorer-qt/share/doc"
ln -s /usr/share/doc/win11-explorer-qt "$stage/opt/win11-explorer-qt/share/doc"
cp -- "$repo/packaging/debian-copyright" "$stage/usr/share/doc/win11-explorer-qt/copyright"
cp -- "$prefix/share/icons/hicolor/scalable/apps/win11-explorer-qt.svg" "$stage/usr/share/icons/hicolor/scalable/apps/"
size_kb="$(du -sk "$stage/opt" | cut -f1)"
# Explicit Depends: the private libs are not known to dpkg, so SHLIBDEPS cannot be used.
# tests/test_user_packaging.py (test_depends_cover_every_system_library...) derives the required set from the
# shipped ELFs (NEEDED -> ldconfig -> dpkg -S -> apt-cache depends --recurse); libOpenGL.so.0 (libfm-qt6) and
# libgssapi_krb5.so.2 (Qt6Network) are NOT pulled in by libgl1/libegl1 etc., hence libopengl0 and libgssapi-krb5-2.
cat > "$stage/DEBIAN/control" <<CONTROL
Package: win11-explorer-qt
Version: $version
Architecture: amd64
Maintainer: Grekto-dev <noreply@invalid>
Section: utils
Priority: optional
Installed-Size: $size_kb
Depends: $libc_dep, $cxx_dep, libglib2.0-0t64 (>= 2.80), libmenu-cache3, libxcb-cursor0, libxkbcommon-x11-0, libxkbcommon0, libx11-xcb1, libxcb-icccm4, libxcb-image0, libxcb-keysyms1, libxcb-render-util0, libxcb-shape0, libxcb-sync1, libxcb-xfixes0, libxcb-xinerama0, libxcb-randr0, libxcb-xkb1, libgl1, libegl1, libopengl0, libgssapi-krb5-2, libfontconfig1, libfreetype6, libharfbuzz0b, libpng16-16t64, zlib1g, libdbus-1-3
Recommends: libgtk-3-0t64, libjpeg-turbo8, gvfs
Description: Windows 11-inspired file manager (fork of PCManFM-Qt)
 Bundles Qt 6.8 and a patched libfm-qt under /opt/win11-explorer-qt.
 Does not register itself as default file manager, MIME handler or autostart.
CONTROL
(cd "$stage" && find opt usr -type f -print0 | sort -z | xargs -0 md5sum > DEBIAN/md5sums)
chmod -R u+rwX,go+rX,go-w,a-s,-t "$stage"
find "$stage" -type d -exec chmod 755 {} +
chmod 755 "$stage/DEBIAN"
dpkg-deb --root-owner-group -Zgzip -z6 --build "$stage" "$out/win11-explorer-qt_${version}_amd64.deb" >/dev/null
printf 'built %s\n' "$out/win11-explorer-qt_${version}_amd64.deb"
