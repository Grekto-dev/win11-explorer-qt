#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-2.0-or-later
# Run the REAL xcb platform plugin of a prefix against a task-owned nested X server.
# Usage: verify-xcb.sh PREFIX DISPLAY      (DISPLAY like :27, a Xephyr started by the caller)
# Refuses :0/:1 (personal displays) and anything that is not a local ":N" display; verifies that
# the X socket's owner process is a Xephyr/Xvfb before any window or input is created.
set -euo pipefail
prefix="${1:-}" display="${2:-}"
if [[ ! "$display" =~ ^:[0-9]+$ ]]; then
    echo "refusing display '$display': only a local task-owned ':N' display is accepted" >&2; exit 2; fi
num="${display#:}"
if ((num < 2)); then
    echo "refusing display $display: :0/:1 are personal displays" >&2; exit 2; fi
[[ -x "$prefix/bin/win11-explorer-qt" ]] || { echo "prefix not found: $prefix" >&2; exit 1; }
sock="/tmp/.X11-unix/X$num"
[[ -S "$sock" ]] || { echo "display $display has no socket $sock" >&2; exit 1; }
owner_pid="$(ss -xlpn 2>/dev/null | awk -v s="$sock" '$5==s || $4==s {print}' | sed -n 's/.*pid=\([0-9]*\).*/\1/p' | head -1)"
[[ -n "$owner_pid" ]] || { echo "cannot resolve the owner of $sock (ss)" >&2; exit 1; }
cmd="$(tr '\0' ' ' < "/proc/$owner_pid/cmdline")"
[[ "$cmd" == *Xephyr* || "$cmd" == *Xvfb* ]] || { echo "owner of $display is not a nested server: $cmd" >&2; exit 1; }
[[ "$(stat -c %u "/proc/$owner_pid")" == "$(id -u)" ]] || { echo "display $display is not owned by this user" >&2; exit 1; }
echo "display $display owned by pid $owner_pid: $cmd"
tmp="$(mktemp -d)"; trap 'rm -rf -- "$tmp"; [[ -z "${app:-}" ]] || kill "$app" 2>/dev/null || true' EXIT
mkdir -p "$tmp/home/run" "$tmp/folder"; chmod 700 "$tmp/home/run"; touch "$tmp/folder/a.txt"
export_env=(env -i PATH=/usr/bin:/bin HOME="$tmp/home" XDG_CONFIG_HOME="$tmp/home/.config" XDG_DATA_HOME="$tmp/home/.local/share"
    XDG_CACHE_HOME="$tmp/home/.cache" XDG_STATE_HOME="$tmp/home/.state" XDG_RUNTIME_DIR="$tmp/home/run"
    DISPLAY="$display" QT_QPA_PLATFORM=xcb LANG=C.UTF-8 NO_AT_BRIDGE=1 LD_LIBRARY_PATH=/nonexistent-poison)
"${export_env[@]}" ldd "$prefix/plugins/platforms/libqxcb.so" | grep -q 'not found' && { echo "libqxcb.so has unresolved libraries" >&2; exit 1; }
"${export_env[@]}" dbus-run-session -- "$prefix/bin/win11-explorer-qt" "$tmp/folder" > "$tmp/app.log" 2>&1 &
app=$!
for _ in $(seq 1 20); do sleep 0.5; DISPLAY="$display" xwininfo -root -tree 2>/dev/null | grep -qi 'win11\|explorer\|folder' && { echo "xcb window mapped on $display"; echo PASS; exit 0; }; done
echo "no application window appeared on $display; log:" >&2; tail -20 "$tmp/app.log" >&2; exit 1
