#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-2.0-or-later
# Build a .deb from a prefix made by make-portable-prefix.sh. Nothing is installed;
# the package only carries /opt/win11-explorer-qt, /usr/bin link, desktop entry and icon.
set -euo pipefail
repo="$(cd -- "$(dirname -- "$(readlink -f -- "${BASH_SOURCE[0]}")")/.." && pwd)"
prefix="" out="$repo/.state/deb" version="2.4.1+win11explorer1" strip_debug=0
while (($#)); do
    case "$1" in
        --prefix) prefix="$2"; shift 2 ;;
        --out) out="$2"; shift 2 ;;
        --version) version="$2"; shift 2 ;;
        --without-debug) strip_debug=1; shift ;;
        -h|--help) printf 'Usage: %s --prefix PREFIX [--out DIR] [--version V] [--without-debug]\n' "$0"; exit 0 ;;
        *) printf 'Unknown option: %s\n' "$1" >&2; exit 2 ;;
    esac
done
[[ -x "$prefix/bin/win11-explorer-qt" && -f "$prefix/share/win11-explorer-qt/win11-explorer-qt.desktop.in" ]] || {
    printf 'Not a prefix from make-portable-prefix.sh: %s\n' "$prefix" >&2; exit 1; }
command -v dpkg-deb >/dev/null || { echo "dpkg-deb not found" >&2; exit 1; }
stage="$(mktemp -d)"
trap 'rm -rf -- "$stage"' EXIT
dest=/opt/win11-explorer-qt
mkdir -p "$stage/DEBIAN" "$stage/opt" "$stage/usr/bin" "$stage/usr/share/applications" \
    "$stage/usr/share/icons/hicolor/scalable/apps" "$out"
cp -a -- "$prefix" "$stage/opt/win11-explorer-qt"
rm -f -- "$stage/opt/win11-explorer-qt/.win11-explorer-qt-prefix"
((strip_debug)) && rm -rf -- "$stage/opt/win11-explorer-qt/lib/debug"
ln -s "$dest/bin/win11-explorer-qt" "$stage/usr/bin/win11-explorer-qt"
text="$(<"$prefix/share/win11-explorer-qt/win11-explorer-qt.desktop.in")"
printf '%s\n' "${text//@BINDIR@/$dest/bin}" > "$stage/usr/share/applications/win11-explorer-qt.desktop"
cp -- "$prefix/share/icons/hicolor/scalable/apps/win11-explorer-qt.svg" "$stage/usr/share/icons/hicolor/scalable/apps/"
size_kb="$(du -sk "$stage/opt" | cut -f1)"
# Explicit Depends: the private libs are not known to dpkg, so SHLIBDEPS cannot be used.
cat > "$stage/DEBIAN/control" <<CONTROL
Package: win11-explorer-qt
Version: $version
Architecture: amd64
Maintainer: Grekto-dev <noreply@invalid>
Section: utils
Priority: optional
Installed-Size: $size_kb
Depends: libc6 (>= 2.39), libstdc++6, libglib2.0-0t64 (>= 2.80), libmenu-cache3, menu-cached, libxcb-cursor0, libxkbcommon-x11-0, libgl1, libegl1, libfontconfig1, libfreetype6, libdbus-1-3
Recommends: libgtk-3-0t64, gvfs
Description: Windows 11-inspired file manager (fork of PCManFM-Qt)
 Bundles Qt 6.8 and a patched libfm-qt under /opt/win11-explorer-qt.
 Does not register itself as default file manager, MIME handler or autostart.
CONTROL
find "$stage" -type d -exec chmod 755 {} +
chmod -R go-w "$stage"
dpkg-deb --root-owner-group -Zgzip -z6 --build "$stage" "$out/win11-explorer-qt_${version}_amd64.deb" >/dev/null
printf 'built %s\n' "$out/win11-explorer-qt_${version}_amd64.deb"
