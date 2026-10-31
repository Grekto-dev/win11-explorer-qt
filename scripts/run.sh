#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-2.0-or-later
set -euo pipefail
repo="$(cd -- "$(dirname -- "$(readlink -f -- "${BASH_SOURCE[0]}")")/.." && pwd)"
if [[ -f "$repo/.deps/env.sh" ]]; then
    source "$repo/.deps/env.sh"
fi
binary="$repo/.state/install/bin/win11-explorer-qt"
if [[ ! -x "$binary" ]]; then
    printf 'Build not installed. Run %s/scripts/build.sh first.\n' "$repo" >&2
    exit 1
fi
export QT_STYLE_OVERRIDE=Fusion
exec "$binary" "$@"
