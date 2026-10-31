#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-2.0-or-later
set -euo pipefail

if [[ $# -ne 1 ]]; then
    echo "usage: $0 /path/to/tst_explorer" >&2
    exit 2
fi
binary=$(realpath "$1")
repo=$(cd "$(dirname "$0")/.." && pwd)
artifacts=${EXPLORER_TEST_ARTIFACTS:-"$repo/.state/selection-xtest"}
mkdir -p "$artifacts"

if DISPLAY=:12 xdpyinfo >/dev/null 2>&1; then
    echo "Refusing to use pre-existing display :12; this test owns its Xephyr display" >&2
    exit 2
fi
if ! DISPLAY=:0 xdpyinfo >/dev/null 2>&1; then
    echo "Xephyr requires the available outer display :0" >&2
    exit 2
fi

Xephyr :12 -screen 1280x800 -ac -br -noreset -nolisten tcp >"$artifacts/xephyr.log" 2>&1 &
xephyr_pid=$!
wm_pid=
cleanup() {
    [[ -z "$wm_pid" ]] || kill "$wm_pid" 2>/dev/null || true
    kill "$xephyr_pid" 2>/dev/null || true
    wait "$wm_pid" 2>/dev/null || true
    wait "$xephyr_pid" 2>/dev/null || true
}
trap cleanup EXIT INT TERM

ready=0
for _ in $(seq 1 100); do
    if DISPLAY=:12 xdpyinfo >/dev/null 2>&1; then ready=1; break; fi
    sleep 0.1
done
if [[ $ready -ne 1 ]]; then
    echo "Xephyr :12 did not become ready" >&2
    exit 1
fi
DISPLAY=:12 metacity --replace >"$artifacts/metacity.log" 2>&1 &
wm_pid=$!
sleep 1

DISPLAY=:12 QT_QPA_PLATFORM=xcb NO_AT_BRIDGE=1 \
    EXPLORER_ENABLE_XTEST=1 EXPLORER_TEST_ARTIFACTS="$artifacts" \
    "$binary" nativeXTestSelectionGestures
