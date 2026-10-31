#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-2.0-or-later
set -euo pipefail
repo="$(cd -- "$(dirname -- "$(readlink -f -- "${BASH_SOURCE[0]}")")/.." && pwd)"
prefix="${HOME:-}"
while (($#)); do
    case "$1" in
        --prefix)
            (($# >= 2)) || { printf '%s\n' 'Missing directory after --prefix.' >&2; exit 2; }
            prefix="$2"
            shift 2
            ;;
        -h|--help)
            printf 'Usage: %s [--prefix HOME_DIR]\n' "$0"
            exit 0
            ;;
        *) printf 'Unknown option: %s\n' "$1" >&2; exit 2 ;;
    esac
done
if [[ -z "$prefix" ]]; then
    printf 'HOME is unset; pass --prefix HOME_DIR.\n' >&2
    exit 2
fi
sources=(
    "$repo/scripts/run.sh"
    "$repo/packaging/win11-explorer-qt.desktop"
    "$repo/icons/explorer/scalable/places/folder.svg"
)
targets=(
    "$prefix/.local/bin/win11-explorer-qt"
    "$prefix/.local/share/applications/win11-explorer-qt.desktop"
    "$prefix/.local/share/icons/hicolor/scalable/apps/win11-explorer-qt.svg"
)
# Validate all targets before writing: never overwrite another file or link.
for index in "${!targets[@]}"; do
    src="${sources[$index]}"
    dest="${targets[$index]}"
    [[ -f "$src" ]] || { printf 'Missing source: %s\n' "$src" >&2; exit 1; }
    if [[ -e "$dest" || -L "$dest" ]]; then
        [[ -L "$dest" && "$(readlink -- "$dest")" == "$src" ]] || {
            printf 'Refusing to overwrite existing target: %s\n' "$dest" >&2; exit 1;
        }
    fi
done
for index in "${!targets[@]}"; do
    src="${sources[$index]}"
    dest="${targets[$index]}"
    mkdir -p -- "$(dirname -- "$dest")"
    if [[ ! -e "$dest" && ! -L "$dest" ]]; then ln -s -- "$src" "$dest"; fi
done
printf 'Installed development links only under %s/.local.\n' "$prefix"
