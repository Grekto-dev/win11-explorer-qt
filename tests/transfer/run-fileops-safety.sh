#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-2.0-or-later
# Runs tst_fileops_safety inside a private HOME/XDG tree that exists BEFORE the private D-Bus
# session starts, so gvfsd-trash (activated by that bus) inherits the temporary HOME and can never
# see the user's real trash. Variables live only in this process (env), never in a shared shell.
set -euo pipefail
tst="${1:?usage: run-fileops-safety.sh <tst_fileops_safety> [args]}"
shift
home="$(mktemp -d "${TMPDIR:-/tmp}/fileops-safety-home-XXXXXX")"
case "$home" in */fileops-safety-home-??????) ;; *) echo "unexpected temp home: $home" >&2; exit 2 ;; esac
# EXIT alone is not enough: a signal (CTest timeout sends TERM, Ctrl-C sends INT, a closed terminal HUP)
# must also clean up, AND must take the whole process tree with it: dbus-run-session does not forward
# signals, so killing only the wrapper would leave the test (or stub) running as an orphan with its HOME
# already deleted. The tree therefore runs in its OWN process group (setsid: pgid == $child). That also
# sidesteps a POSIX trap: an asynchronous child of a non-interactive bash ignores SIGINT, so forwarding INT
# to it is a no-op and `wait` would hang. The whole group gets TERM, then KILL after a deadline.
# SIGKILL of the wrapper itself cannot be trapped; the leftover is then only a private temp dir.
grace="${FILEOPS_SAFETY_KILL_GRACE:-10}"
case "$grace" in ''|*[!0-9]*) grace=10 ;; esac
child=""
group_alive() {  # any non-zombie process left in the group?
    ps -o stat= -g "$child" 2>/dev/null | grep -qv '^Z'
}
kill_group() {  # TERM, wait up to $grace seconds (polling every 0.1 s), then KILL; always returns 0
    [ -n "$child" ] || return 0
    kill -TERM -- "-$child" 2>/dev/null || true
    local i
    for ((i = 0; i < grace * 10; i++)); do
        group_alive || return 0
        sleep 0.1
    done
    kill -KILL -- "-$child" 2>/dev/null || true
    for ((i = 0; i < 50; i++)); do
        group_alive || return 0
        sleep 0.1
    done
    return 0
}
on_signal() {  # tear the process group down, then leave (the EXIT trap removes the temp home)
    trap '' INT TERM HUP
    kill_group
    exit "$1"
}
cleanup() {
    trap '' INT TERM HUP
    # normal exit too: no daemon started under the private bus may outlive the run
    [ -z "$child" ] || ! group_alive || kill_group
    # gvfsd-fuse may leave its mountpoint behind; unmount only inside our own temporary home.
    for m in "$home/run/gvfs"; do
        if mountpoint -q "$m" 2>/dev/null; then fusermount3 -uz "$m" 2>/dev/null || fusermount -uz "$m" 2>/dev/null || true; fi
    done
    rm -rf --one-file-system -- "$home" || true
}
trap cleanup EXIT
trap 'on_signal 130' INT
trap 'on_signal 143' TERM
trap 'on_signal 129' HUP
mkdir -p "$home/data" "$home/config" "$home/cache" "$home/state" "$home/run"
chmod 700 "$home/run"
# The test refuses to run unless the bus is the one started right here: the inherited address is passed
# along only so it can be compared, and `env FILEOPS_SAFETY_BUS=1` runs INSIDE dbus-run-session (the test
# also checks that its parent process is dbus-run-session).
inherited_bus="${DBUS_SESSION_BUS_ADDRESS:-}"
env -u DBUS_SESSION_BUS_ADDRESS \
    FILEOPS_SAFETY_INHERITED_BUS="$inherited_bus" \
    HOME="$home" XDG_DATA_HOME="$home/data" XDG_CONFIG_HOME="$home/config" \
    XDG_CACHE_HOME="$home/cache" XDG_STATE_HOME="$home/state" XDG_RUNTIME_DIR="$home/run" \
    FILEOPS_SAFETY_HOME="$home" QT_QPA_PLATFORM=offscreen GVFS_DISABLE_FUSE=1 \
    setsid dbus-run-session -- env FILEOPS_SAFETY_BUS=1 "$tst" "$@" &
child=$!
# setsid only execs in place (pgid == pid) when it is not already a group leader; if it ever forked we could not
# address the group, so refuse rather than run a test that cannot be torn down.
if [ "$(ps -o pgid= -p "$child" 2>/dev/null | tr -d ' ')" != "$child" ]; then
    kill -KILL "$child" 2>/dev/null || true
    echo "run-fileops-safety: cannot put the test in its own process group" >&2
    exit 2
fi
rc=0
wait "$child" || rc=$?
exit "$rc"
