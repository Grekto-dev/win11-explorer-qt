#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Validate the loader's physical libfm identity without executing app main."""
from pathlib import Path
import re
import subprocess
import sys


def validate(prefix, binary, manifest):
    prefix = Path(prefix).resolve(strict=True)
    lines = Path(manifest).read_text().splitlines()
    if len(lines) != 1 or not Path(lines[0]).is_absolute():
        raise ValueError('invalid configured artifact manifest')
    artifact = Path(lines[0]).resolve(strict=True)
    if not artifact.is_relative_to(prefix):
        raise ValueError(f'configured artifact outside selected prefix: {artifact}')
    dynamic = subprocess.run(['readelf', '-d', str(artifact)], check=True,
                             text=True, capture_output=True).stdout
    names = re.findall(r'\(SONAME\)[^\n]*\[([^]/]+)\]', dynamic)
    if len(names) != 1:
        raise ValueError('missing/ambiguous ELF SONAME')
    soname = names[0]
    soname_file = (prefix / 'lib' / soname).resolve(strict=True)
    if soname_file != artifact:
        raise ValueError(f'SONAME resolves to {soname_file}, expected {artifact}')
    trace = subprocess.run(['ldd', str(binary)], check=True, text=True, capture_output=True).stdout
    loaded = re.findall(r'^\s*libfm-qt6\.so[^\s]*\s+=>\s+(.+?)\s+\(0x[0-9a-fA-F]+\)',
                        trace, re.MULTILINE)
    if len(loaded) != 1 or Path(loaded[0]).resolve(strict=True) != artifact:
        raise ValueError(f'loader did not select configured artifact {artifact}: {trace.strip()}')
    if not re.search(r'^\s*' + re.escape(soname) + r'\s+=>', trace, re.MULTILINE):
        raise ValueError(f'consumer does not request configured SONAME {soname}')


if __name__ == '__main__':
    try:
        validate(*sys.argv[1:])
    except (ValueError, OSError, subprocess.SubprocessError) as error:
        print(f'Runtime ABI mismatch: {error}', file=sys.stderr)
        sys.exit(1)
