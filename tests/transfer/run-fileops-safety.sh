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
    rm -rf -- "$home" || true
}
trap cleanup EXIT
mkdir -p "$home/data" "$home/config" "$home/cache" "$home/state" "$home/run"
chmod 700 "$home/run"
env -u DBUS_SESSION_BUS_ADDRESS \
    HOME="$home" XDG_DATA_HOME="$home/data" XDG_CONFIG_HOME="$home/config" \
    XDG_CACHE_HOME="$home/cache" XDG_STATE_HOME="$home/state" XDG_RUNTIME_DIR="$home/run" \
    FILEOPS_SAFETY_HOME="$home" QT_QPA_PLATFORM=offscreen GVFS_DISABLE_FUSE=1 \
    dbus-run-session -- "$tst" "$@"
