#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-2.0-or-later
# Run the REAL xcb platform plugin of a prefix against a task-owned nested X server.
# Usage: verify-xcb.sh PREFIX DISPLAY      (DISPLAY like :27, a Xephyr started by the caller)
# Refuses :0/:1 (personal displays) and anything that is not a local ":N" display; verifies that
# the owner of BOTH the path and the abstract X socket is a Xephyr/Xvfb executable (/proc/PID/exe) before any window or input is created.
set -euo pipefail
prefix="${1:-}" display="${2:-}"
if [[ ! "$display" =~ ^:[0-9]+$ ]]; then
    echo "refusing display '$display': only a local task-owned ':N' display is accepted" >&2; exit 2; fi
num="${display#:}"
if ((num < 2)); then
    echo "refusing display $display: :0/:1 are personal displays" >&2; exit 2; fi
[[ -x "$prefix/bin/win11-explorer-qt" ]] || { echo "prefix not found: $prefix" >&2; exit 1; }
sockdir="${VERIFY_XCB_SOCKET_DIR:-/tmp/.X11-unix}"   # override only for the unit test with fake servers
sock="$sockdir/X$num"
asock="@$sockdir/X$num"   # libxcb tries the ABSTRACT socket first: it is a different namespace
[[ -S "$sock" ]] || { echo "display $display has no socket $sock" >&2; exit 1; }
owner_of() { ss -xlpn 2>/dev/null | awk -v s="$1" '$5==s {print}' | sed -n 's/.*pid=\([0-9]*\).*/\1/p' | head -1; }
# The owner must be an X server executable (basename of /proc/PID/exe, not a substring of the cmdline,
# which any process of this user can fake) of this user.
check_owner() {
    local pid="$1" what="$2" exe
    exe="$(readlink "/proc/$pid/exe" 2>/dev/null || true)"; exe="${exe% (deleted)}"
    case "${exe##*/}" in Xephyr|Xvfb) ;; *) echo "owner of $what ($display) is not a nested server: pid $pid exe '${exe:-?}'" >&2; exit 1 ;; esac
    [[ "$(stat -c %u "/proc/$pid")" == "$(id -u)" ]] || { echo "$what of display $display is not owned by this user" >&2; exit 1; }
}
owner_pid="$(owner_of "$sock")"
[[ -n "$owner_pid" ]] || { echo "cannot resolve the owner of $sock (ss)" >&2; exit 1; }
check_owner "$owner_pid" "socket"
aowner="$(owner_of "$asock")"
if [[ -n "$aowner" ]]; then
    [[ "$aowner" == "$owner_pid" ]] || { echo "abstract socket $asock belongs to pid $aowner, not to the owner $owner_pid of $sock" >&2; exit 1; }
elif grep -qF -- " $asock" /proc/net/unix 2>/dev/null && [[ -z "$(ss -xlpn 2>/dev/null | awk -v s="$asock" '$5==s && /pid=/')" ]]; then
    # fail closed: the abstract name is taken (libxcb connects to it FIRST) but its owner is not visible (other uid)
    echo "abstract socket $asock exists but its owner pid is not visible: cannot prove it is the nested server" >&2; exit 1
fi
echo "display $display owned by pid $owner_pid: $(readlink "/proc/$owner_pid/exe") (abstract socket: ${aowner:+same pid $aowner}${aowner:-not listed})"
if [[ -n "${VERIFY_XCB_OWNER_CHECK_ONLY:-}" ]]; then   # test hook; deliberately NOT a success
    echo "OWNER-CHECK-ONLY (no app run): the owner check passed but no application was started; this is NOT a success"
    exit 3
fi
tmp="$(mktemp -d)"
# The app (env -> dbus-run-session -> dbus-daemon + app) runs in its own session/process group, so that
# the cleanup can kill the whole group instead of just the first process.
stop_group() {
    [[ -z "${app:-}" ]] && return 0
    kill -TERM -- "-$app" 2>/dev/null || true
    for _ in 1 2 3 4 5 6 7 8 9 10; do kill -0 -- "-$app" 2>/dev/null || break; sleep 0.2; done
    kill -KILL -- "-$app" 2>/dev/null || true
}
trap 'stop_group; rm -rf -- "$tmp"' EXIT
mkdir -p "$tmp/home/run" "$tmp/folder"; chmod 700 "$tmp/home/run"; touch "$tmp/folder/a.txt"
export_env=(env -i PATH=/usr/bin:/bin HOME="$tmp/home" XDG_CONFIG_HOME="$tmp/home/.config" XDG_DATA_HOME="$tmp/home/.local/share"
    XDG_CACHE_HOME="$tmp/home/.cache" XDG_STATE_HOME="$tmp/home/.state" XDG_RUNTIME_DIR="$tmp/home/run"
    DISPLAY="$display" QT_QPA_PLATFORM=xcb LANG=C.UTF-8 NO_AT_BRIDGE=1 LD_LIBRARY_PATH=/nonexistent-poison)
"${export_env[@]}" ldd "$prefix/plugins/platforms/libqxcb.so" | grep -q 'not found' && { echo "libqxcb.so has unresolved libraries" >&2; exit 1; }
setsid "${export_env[@]}" dbus-run-session -- "$prefix/bin/win11-explorer-qt" "$tmp/folder" > "$tmp/app.log" 2>&1 &
app=$!
for _ in $(seq 1 20); do sleep 0.5; DISPLAY="$display" xwininfo -root -tree 2>/dev/null | grep -qi 'win11\|explorer\|folder' && { echo "xcb window mapped on $display"; echo PASS; exit 0; }; done
echo "no application window appeared on $display; log:" >&2; tail -20 "$tmp/app.log" >&2; exit 1
