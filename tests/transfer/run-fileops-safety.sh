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
cleanup() {
    # gvfsd-fuse may leave its mountpoint behind; unmount only inside our own temporary home.
    for m in "$home/run/gvfs"; do
        if mountpoint -q "$m" 2>/dev/null; then fusermount3 -uz "$m" 2>/dev/null || fusermount -uz "$m" 2>/dev/null || true; fi
    done
    rm -rf --one-file-system -- "$home" || true
}
# EXIT alone is not enough: a signal (CTest timeout sends TERM, Ctrl-C sends INT, a closed terminal HUP)
# must also clean up. SIGKILL cannot be trapped; the leftover is then only a private temp dir.
child=""
on_signal() {  # forward to the running test, wait for it, then leave (the EXIT trap removes the temp home)
    [ -z "$child" ] || kill -"$1" "$child" 2>/dev/null || true
    [ -z "$child" ] || wait "$child" 2>/dev/null || true
    exit "$2"
}
trap cleanup EXIT
trap 'on_signal INT 130' INT
trap 'on_signal TERM 143' TERM
trap 'on_signal HUP 129' HUP
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
    dbus-run-session -- env FILEOPS_SAFETY_BUS=1 "$tst" "$@" &
child=$!
rc=0
wait "$child" || rc=$?
exit "$rc"
