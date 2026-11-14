#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Render packaging/win11-explorer-qt.desktop.in with a safely quoted absolute bin directory.

Desktop Entry spec: an Exec argument containing reserved characters must be double-quoted;
inside the quotes `"`, backtick, `$` and `\\` take a backslash; the string value level then
doubles every backslash; a literal `%` is written `%%`.
Usage: render_desktop.py TEMPLATE BINDIR  (prints the entry)"""
import sys


def exec_quote(path):
    if any(ord(c) < 32 or ord(c) == 127 for c in path):
        raise ValueError('control characters (newline, tab, NUL...) are not allowed in the install path')
    s = ''.join('\\' + c if c in '"`$\\' else c for c in path)
    s = s.replace('%', '%%')
    s = s.replace('\\', '\\\\')
    return '"' + s + '"'


def render(template, bindir):
    exec_quote(bindir)  # validates
    out = []
    for line in template.splitlines():
        if line.startswith('Exec=@BINDIR@/'):
            line = 'Exec=' + exec_quote(bindir.rstrip('/') + '/' + line[len('Exec=@BINDIR@/'):].split(' ', 1)[0]) \
                + (' ' + line.split(' ', 1)[1] if ' ' in line else '')
        elif '@BINDIR@' in line:
            raise ValueError(f'unsupported placeholder use: {line}')
        out.append(line)
    return '\n'.join(out) + '\n'


if __name__ == '__main__':
    try:
        sys.stdout.write(render(open(sys.argv[1], encoding='utf-8').read(), sys.argv[2]))
    except (ValueError, OSError, IndexError) as e:
        print(f'error: {e}', file=sys.stderr)
        sys.exit(1)
