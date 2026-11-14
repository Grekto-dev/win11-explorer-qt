#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-2.0-or-later
# Installs the launcher, a real .desktop entry and the icon of an already
# assembled prefix (scripts/make-portable-prefix.sh) under ~/.local. Touches
# only its own three files, never MIME defaults, mimeapps.list, autostart or
# any cache database.
set -euo pipefail
name=win11-explorer-qt
home_dir="${HOME:-}"
prefix=""
mode=install
dry=0
usage() { printf 'Usage: %s [--prefix DIR] [--uninstall] [--dry-run]\n  --prefix defaults to $HOME/.local/opt/%s\n' "$0" "$name"; }
while (($#)); do
    case "$1" in
        --prefix) (($# >= 2)) || { echo "--prefix needs a directory" >&2; exit 2; }; prefix="$2"; shift 2 ;;
        --uninstall) mode=uninstall; shift ;;
        --dry-run) dry=1; shift ;;
        -h|--help) usage; exit 0 ;;
        *) printf 'Unknown option: %s\n' "$1" >&2; usage >&2; exit 2 ;;
    esac
done
[[ -n "$home_dir" ]] || { echo "HOME is unset" >&2; exit 2; }
[[ -n "$prefix" ]] || prefix="$home_dir/.local/opt/$name"
[[ "$prefix" = /* ]] || { echo "--prefix must be an absolute path: $prefix" >&2; exit 2; }
prefix="${prefix%/}"
launcher="$home_dir/.local/bin/$name"
desktop="$home_dir/.local/share/applications/$name.desktop"
icon="$home_dir/.local/share/icons/hicolor/scalable/apps/$name.svg"
src_bin="$prefix/bin/$name"
src_desktop="$prefix/share/$name/$name.desktop.in"
src_icon="$prefix/share/icons/hicolor/scalable/apps/$name.svg"
say() { if ((dry)); then printf '[dry-run] %s\n' "$*"; else printf '%s\n' "$*"; fi; }

render_desktop() { # stdout: template with the absolute bin directory
    local text; text="$(<"$src_desktop")"
    printf '%s\n' "${text//@BINDIR@/$prefix/bin}"
}
# A file is ours when it is our launcher link or carries our StartupWMClass.
is_ours_link() { [[ -L "$1" && "$(readlink -- "$1")" == "$src_bin" ]]; }
is_ours_file() { [[ -f "$1" && ! -L "$1" ]] && grep -qx "StartupWMClass=$name" "$1"; }
is_ours_icon() { [[ -f "$1" && ! -L "$1" ]] && cmp -s "$1" "$src_icon"; }

if [[ "$mode" == uninstall ]]; then
    for item in "$launcher:link" "$desktop:file" "$icon:icon"; do
        path="${item%:*}"; kind="${item##*:}"
        if [[ ! -e "$path" && ! -L "$path" ]]; then say "absent: $path"; continue; fi
        ours=0
        case "$kind" in
            link) is_ours_link "$path" && ours=1 ;;
            file) is_ours_file "$path" && ours=1 ;;
            icon) { [[ -f "$path" ]] && { [[ ! -f "$src_icon" ]] || is_ours_icon "$path"; } && grep -q 'Original artwork for this fork' "$path"; } && ours=1 ;;
        esac
        if ((ours)); then say "remove: $path"; ((dry)) || rm -f -- "$path"; else say "kept (not ours): $path"; fi
    done
    exit 0
fi

[[ -x "$src_bin" ]] || { printf 'Not a prefix: missing executable %s\n' "$src_bin" >&2; exit 1; }
[[ -f "$src_desktop" ]] || { printf 'Missing %s\n' "$src_desktop" >&2; exit 1; }
[[ -f "$src_icon" ]] || { printf 'Missing %s\n' "$src_icon" >&2; exit 1; }
# Validate every target before writing anything.
if [[ -e "$launcher" || -L "$launcher" ]]; then
    { [[ -L "$launcher" ]] && [[ "$(readlink -- "$launcher")" == */bin/$name ]]; } || { printf 'Refusing to overwrite: %s\n' "$launcher" >&2; exit 1; }
fi
if [[ -e "$desktop" || -L "$desktop" ]]; then
    is_ours_file "$desktop" || { printf 'Refusing to overwrite: %s\n' "$desktop" >&2; exit 1; }
fi
if [[ -e "$icon" || -L "$icon" ]]; then
    { [[ -f "$icon" && ! -L "$icon" ]] && grep -q 'Original artwork for this fork' "$icon"; } || { printf 'Refusing to overwrite: %s\n' "$icon" >&2; exit 1; }
fi
say "launcher: $launcher -> $src_bin"
say "desktop:  $desktop (Exec=$prefix/bin/$name %U)"
say "icon:     $icon"
((dry)) && exit 0
mkdir -p -- "$(dirname -- "$launcher")" "$(dirname -- "$desktop")" "$(dirname -- "$icon")"
ln -sfn -- "$src_bin" "$launcher"
tmp="$(mktemp "$desktop.XXXXXX")"
render_desktop > "$tmp"
chmod 644 "$tmp"
mv -f -- "$tmp" "$desktop"
cp -f -- "$src_icon" "$icon"
printf 'Installed %s (no MIME default registered, no autostart).\n' "$name"
