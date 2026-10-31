#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-2.0-or-later
set -euo pipefail
if [[ $# -ne 1 ]]; then
    echo "usage: $0 /path/to/tst_explorer" >&2
    exit 2
fi
repo=$(cd "$(dirname "$0")/.." && pwd)
exec /usr/bin/python3 "$repo/tests/run_selection_x11.py" \
    --binary "$1" --display "${EXPLORER_XTEST_DISPLAY-:18}" \
    --artifacts "${EXPLORER_TEST_ARTIFACTS:-$repo/.state/selection-xtest}"
