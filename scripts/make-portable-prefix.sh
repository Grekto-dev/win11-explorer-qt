#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-2.0-or-later
# Assemble a relocatable prefix (default ~/.local/opt/win11-explorer-qt) from the
# build produced by scripts/build.sh. Writes ONLY inside the chosen prefix, and
# only replaces an existing directory that carries this script's marker file.
set -euo pipefail
repo="$(cd -- "$(dirname -- "$(readlink -f -- "${BASH_SOURCE[0]}")")/.." && pwd)"
prefix="${HOME:-}/.local/opt/win11-explorer-qt"
install_dir="$repo/.state/install"
overlay="$repo/.state/libfm-overlay-prefix"
while (($#)); do
    case "$1" in
        --prefix) (($# >= 2)) || { echo "--prefix needs a directory" >&2; exit 2; }; prefix="$2"; shift 2 ;;
        --install-dir) install_dir="$2"; shift 2 ;;
        --overlay) overlay="$2"; shift 2 ;;
        -h|--help) printf 'Usage: %s [--prefix DIR] [--install-dir DIR] [--overlay DIR]\n' "$0"; exit 0 ;;
        *) printf 'Unknown option: %s\n' "$1" >&2; exit 2 ;;
    esac
done
exec /usr/bin/python3 "$repo/scripts/portable_prefix.py" --repo "$repo" --prefix "$prefix" \
    --install-dir "$install_dir" --overlay "$overlay"
