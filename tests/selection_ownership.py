# SPDX-License-Identifier: GPL-2.0-or-later
"""Linux accidental-concurrency guard, not a same-UID security sandbox."""
import fcntl
import os
from pathlib import Path
import re
import socket
import struct


def display_number(target):
    if not re.fullmatch(r':(?:[2-9]|[1-9][0-9]+)', target):
        raise RuntimeError('Unsafe isolated display: ' + target)
    return int(target[1:])


def process_identity(pid):
    text = Path(f'/proc/{pid}/stat').read_text()
    fields = text[text.rfind(')') + 2:].split()
    if fields[0] == 'Z':
        raise RuntimeError('Dead process')
    return int(fields[1]), fields[19]


def reserve(target, scratch):
    number = display_number(target)
    # Never unlink: unlink/recreate would allow two locks on different inodes.
    lock = (scratch / f'explorer-x11-display-{number}.lock').open('a')
    try:
        fcntl.flock(lock, fcntl.LOCK_EX | fcntl.LOCK_NB)
    except BlockingIOError:
        lock.close()
        raise RuntimeError(f'Display {target} is reserved by another runner') from None
    return lock


def check_free(target):
    number = display_number(target)
    if Path(f'/tmp/.X{number}-lock').exists() or Path(f'/tmp/.X11-unix/X{number}').exists():
        raise RuntimeError(f'Refusing pre-existing display {target}')
    with socket.socket(socket.AF_UNIX) as sock:
        if sock.connect_ex('\0/tmp/.X11-unix/X' + str(number)) == 0:
            raise RuntimeError(f'Refusing pre-existing display {target}')


def verify_server(target, server_pid, server_start, owner_pid, owner_start):
    number = display_number(target)
    parent, start = process_identity(server_pid)
    if parent != owner_pid or start != server_start or process_identity(owner_pid)[1] != owner_start:
        raise RuntimeError('Xephyr PID/starttime/parent does not match wrapper')
    exe = Path(f'/proc/{server_pid}/exe').resolve(strict=True)
    args = Path(f'/proc/{server_pid}/cmdline').read_bytes().split(b'\0')
    if exe.name != 'Xephyr' or target.encode() not in args:
        raise RuntimeError('Server executable/display identity mismatch')
    ancestor = os.getpid()
    while ancestor > 1 and ancestor != owner_pid:
        ancestor = process_identity(ancestor)[0]
    if ancestor != owner_pid:
        raise RuntimeError('Wrapper is not an ancestor of this process')
    with socket.socket(socket.AF_UNIX) as sock:
        try:
            sock.connect('\0/tmp/.X11-unix/X' + str(number))
        except OSError:
            sock.connect('/tmp/.X11-unix/X' + str(number))
        pid, uid, _ = struct.unpack('3i', sock.getsockopt(socket.SOL_SOCKET, socket.SO_PEERCRED, 12))
        if pid != server_pid or uid != os.getuid():
            raise RuntimeError('Connected Xserver peer does not belong to this wrapper')
    # Detect death/PID reuse during connection/readiness as well.
    if process_identity(server_pid) != (owner_pid, server_start):
        raise RuntimeError('Xephyr died during ownership validation')


def verify_environment(env):
    verify_server(env['DISPLAY'], int(env['EXPLORER_XSERVER_PID']), env['EXPLORER_XSERVER_START'],
                  int(env['EXPLORER_XOWNER_PID']), env['EXPLORER_XOWNER_START'])
