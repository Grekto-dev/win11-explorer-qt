#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-2.0-or-later
set -euo pipefail
repo="$(cd -- "$(dirname -- "$(readlink -f -- "${BASH_SOURCE[0]}")")/.." && pwd)"
if [[ -f "$repo/.deps/env.sh" ]]; then
    source "$repo/.deps/env.sh"
fi
binary="$repo/.state/install/bin/win11-explorer-qt"
overlay="${LIBFMQT_OVERLAY_PREFIX:-$repo/.state/libfm-overlay-prefix}"
overlay="$(cd "$overlay" 2>/dev/null && pwd -P)" || {
    printf 'Private libfm-qt runtime overlay not found. Build first: %s/scripts/build.sh\n' "$repo" >&2
    exit 1
}
case "$overlay" in
    "$repo"/.state/*) ;;
    *) printf 'Refusing runtime prefix outside this worktree .state/: %s\n' "$overlay" >&2; exit 1 ;;
esac
if [[ ! -x "$binary" ]]; then
    printf 'Build not installed. Run %s/scripts/build.sh first.\n' "$repo" >&2
    exit 1
fi
export LD_LIBRARY_PATH="$overlay/lib${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"
/usr/bin/python3 "$repo/scripts/fidelity/check-private-libfm-runtime.py" \
    "$overlay" "$binary" \
    "$repo/.state/install/share/win11-explorer-qt/private-libfm-runtime.txt"
export QT_STYLE_OVERRIDE=Fusion
exec "$binary" "$@"
