#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-2.0-or-later
# Installs the launcher, a real .desktop entry and the icon of an already
# assembled prefix (scripts/make-portable-prefix.sh) under ~/.local. Touches
# only its own three files plus the derived cache of the user's applications
# directory (update-desktop-database, so "Open with" lists the app). Never writes
# mimeapps.list, xdg-mime defaults, autostart or anything of Nemo/Cinnamon.
set -euo pipefail
here="$(cd -- "$(dirname -- "$(readlink -f -- "${BASH_SOURCE[0]}")")" && pwd)"
name=win11-explorer-qt
home_dir="${HOME:-}"
prefix=""
prefix_given=0
mode=install
dry=0
desktop_db=1
usage() { printf 'Usage: %s [--prefix DIR] [--uninstall] [--dry-run] [--no-desktop-db]\n  --prefix defaults to $HOME/.local/opt/%s\n  --no-desktop-db  skip update-desktop-database for ~/.local/share/applications\n' "$0" "$name"; }
while (($#)); do
    case "$1" in
        --prefix) (($# >= 2)) || { echo "--prefix needs a directory" >&2; exit 2; }; prefix="$2"; prefix_given=1; shift 2 ;;
        --uninstall) mode=uninstall; shift ;;
        --dry-run) dry=1; shift ;;
        --no-desktop-db) desktop_db=0; shift ;;
        -h|--help) usage; exit 0 ;;
        *) printf 'Unknown option: %s\n' "$1" >&2; usage >&2; exit 2 ;;
    esac
done
[[ -n "$home_dir" && "$home_dir" = /* && "$home_dir" != / && "$home_dir" != *[[:cntrl:]]* ]] || {
    echo "HOME must be a non-empty absolute directory other than / (got: '${home_dir}')" >&2; exit 2; }
home_dir="$(realpath -m -- "$home_dir")"
[[ "$home_dir" != / ]] || { echo "HOME resolves to /; refusing" >&2; exit 2; }
if ((prefix_given)); then
    [[ -n "$prefix" ]] || { echo "--prefix is empty" >&2; exit 2; }
else
    prefix="$home_dir/.local/opt/$name"
fi
[[ "$prefix" = /* ]] || { echo "--prefix must be an absolute path: $prefix" >&2; exit 2; }
[[ "$prefix" != *[[:cntrl:]]* ]] || { echo "--prefix must not contain control characters (newline, tab...)" >&2; exit 2; }
prefix="$(realpath -m -- "$prefix")"
[[ "$prefix" != / ]] || { echo "--prefix resolves to /; refusing" >&2; exit 2; }
launcher="$home_dir/.local/bin/$name"
apps="$home_dir/.local/share/applications"
desktop="$apps/$name.desktop"
icon="$home_dir/.local/share/icons/hicolor/scalable/apps/$name.svg"
src_bin="$prefix/bin/$name"
src_desktop="$prefix/share/$name/$name.desktop.in"
src_icon="$prefix/share/icons/hicolor/scalable/apps/$name.svg"
marker=.win11-explorer-qt-prefix
say() { if ((dry)); then printf '[dry-run] %s\n' "$*"; else printf '%s\n' "$*"; fi; }
tmp=""
trap '[[ -z "$tmp" ]] || rm -f -- "$tmp"' EXIT

refresh_desktop_db() {
    ((desktop_db)) || return 0
    if ((dry)); then say "would run: update-desktop-database -q $apps"; return 0; fi
    if command -v update-desktop-database >/dev/null 2>&1; then
        update-desktop-database -q "$apps" || echo "warning: update-desktop-database failed (Open with may not list the app)" >&2
    else
        echo "note: update-desktop-database not found (package desktop-file-utils); 'Open with' may not list the app" >&2
    fi
}
# Our launcher link: points at the chosen prefix, or at *any* prefix this tool made (marker present), or dangles at */bin/name.
is_ours_link() {
    [[ -L "$1" ]] || return 1
    local t; t="$(readlink -- "$1")"
    [[ "$t" == "$src_bin" ]] && return 0
    [[ "$t" == */bin/$name ]] || return 1
    [[ ! -e "$t" ]] && return 0
    [[ -f "${t%/bin/$name}/$marker" ]]
}
is_ours_file() { [[ -f "$1" && ! -L "$1" ]] && grep -qx "StartupWMClass=$name" "$1" && grep -q "^Name=Win11 Explorer Qt" "$1"; }
is_ours_icon() { [[ -f "$1" && ! -L "$1" ]] && grep -q 'Original artwork for this fork' "$1"; }

if [[ "$mode" == uninstall ]]; then
    changed=0
    for item in "$launcher:link" "$desktop:file" "$icon:icon"; do
        path="${item%:*}"; kind="${item##*:}"
        if [[ ! -e "$path" && ! -L "$path" ]]; then say "absent: $path"; continue; fi
        ours=0
        case "$kind" in
            link) is_ours_link "$path" && ours=1 ;;
            file) is_ours_file "$path" && ours=1 ;;
            icon) is_ours_icon "$path" && ours=1 ;;
        esac
        if ((ours)); then say "remove: $path"; ((dry)) || { rm -f -- "$path"; changed=1; }; else say "kept (not ours): $path"; fi
    done
    if ((changed || dry)) && [[ -d "$apps" ]]; then refresh_desktop_db; fi
    exit 0
fi

[[ -x "$src_bin" ]] || { printf 'Not a prefix: missing executable %s\n' "$src_bin" >&2; exit 1; }
[[ -f "$src_desktop" ]] || { printf 'Missing %s\n' "$src_desktop" >&2; exit 1; }
[[ -f "$src_icon" ]] || { printf 'Missing %s\n' "$src_icon" >&2; exit 1; }
# Validate every target and render the entry BEFORE writing anything.
if [[ -e "$launcher" || -L "$launcher" ]]; then
    is_ours_link "$launcher" || { printf 'Refusing to overwrite: %s\n' "$launcher" >&2; exit 1; }
fi
if [[ -e "$desktop" || -L "$desktop" ]]; then
    is_ours_file "$desktop" || { printf 'Refusing to overwrite: %s\n' "$desktop" >&2; exit 1; }
fi
if [[ -e "$icon" || -L "$icon" ]]; then
    is_ours_icon "$icon" || { printf 'Refusing to overwrite: %s\n' "$icon" >&2; exit 1; }
fi
rendered="$(/usr/bin/python3 "$here/render_desktop.py" "$src_desktop" "$prefix/bin")" || { echo "cannot render the desktop entry" >&2; exit 1; }
say "launcher: $launcher -> $src_bin"
say "desktop:  $desktop"
say "$(printf '%s\n' "$rendered" | grep -m1 '^Exec=')"
say "icon:     $icon"
if ((dry)); then refresh_desktop_db; exit 0; fi
mkdir -p -- "$(dirname -- "$launcher")" "$apps" "$(dirname -- "$icon")"
tmp="$(mktemp "$desktop.XXXXXX")"
printf '%s\n' "$rendered" > "$tmp"
chmod 644 "$tmp"
cp -f -- "$src_icon" "$icon"
mv -f -- "$tmp" "$desktop"; tmp=""
ln -sfn -- "$src_bin" "$launcher"
refresh_desktop_db
printf 'Installed %s (no MIME default registered, no autostart).\n' "$name"
