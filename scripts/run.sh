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
if [[ ! -f "$overlay/lib/libfm-qt6.so.17" && ! -f "$overlay/lib/libfm-qt6.so.17.0.0" ]]; then
    printf 'Private libfm-qt runtime overlay missing under %s; refusing to start against another ABI.\n' "$overlay" >&2
    exit 1
fi
export LD_LIBRARY_PATH="$overlay/lib${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"
resolved="$(ldd "$binary" | while IFS= read -r line; do
    case "$line" in
        *libfm-qt6.so*) printf '%s\n' "$line"; break ;;
    esac
done)"
case "$resolved" in
    *"$overlay/lib/"*) ;;
    *) printf 'Runtime ABI mismatch: expected %s/lib/libfm-qt6.so, ldd resolved: %s\n' "$overlay" "${resolved:-not found}" >&2; exit 1 ;;
esac
export QT_STYLE_OVERRIDE=Fusion
exec "$binary" "$@"
