#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Minimum glibc / libstdc++ implied by the undefined versioned symbols of every ELF under PREFIX.
Prints: glibc=<X.Y> glibcxx=<3.4.N or -> libstdcxx_pkg=<gcc version or ->"""
import re
import subprocess
import sys
from pathlib import Path

# libstdc++.so.6 symbol version -> first GCC release that provides it (Debian package version floor)
GLIBCXX_TO_GCC = {24: '5.2', 25: '8.1', 26: '9.1', 27: '9.2', 28: '10.1', 29: '11.1', 30: '12.1', 31: '13.1', 32: '13.2', 33: '14.1'}


def is_elf(p):
    try:
        with open(p, 'rb') as f:
            return f.read(4) == b'\x7fELF' and not p.is_symlink()
    except OSError:
        return False


def requirements(prefix):
    glibc, glibcxx = (0, 0), 0
    for p in sorted(Path(prefix).rglob('*')):
        if not p.is_file() or p.is_symlink() or '/debug/' in str(p) or not is_elf(p):
            continue
        out = subprocess.run(['objdump', '-T', str(p)], text=True, capture_output=True).stdout
        for line in out.splitlines():
            if '*UND*' not in line:
                continue
            m = re.search(r'\(GLIBC_(\d+)\.(\d+)(?:\.\d+)?\)', line)
            if m:
                glibc = max(glibc, (int(m.group(1)), int(m.group(2))))
            m = re.search(r'\(GLIBCXX_3\.4\.(\d+)\)', line)
            if m:
                glibcxx = max(glibcxx, int(m.group(1)))
    return glibc, glibcxx


def main():
    glibc, glibcxx = requirements(sys.argv[1])
    gcc = '-'
    if glibcxx:
        gcc = GLIBCXX_TO_GCC.get(glibcxx) or (GLIBCXX_TO_GCC[24] if glibcxx < 24 else None)
        if gcc is None:
            print(f'error: GLIBCXX_3.4.{glibcxx} is newer than the known table; extend GLIBCXX_TO_GCC', file=sys.stderr)
            sys.exit(1)
    print(f'glibc={glibc[0]}.{glibc[1]}' if glibc != (0, 0) else 'glibc=-', f'glibcxx={"3.4." + str(glibcxx) if glibcxx else "-"}', f'libstdcxx_pkg={gcc}')


if __name__ == '__main__':
    main()
