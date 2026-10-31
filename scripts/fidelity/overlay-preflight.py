#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Fail closed before overlay writes; paths must have no symlink ancestors.

This guards static misconfiguration, not a hostile concurrent filesystem writer.
"""
import os
from pathlib import Path
import sys


def validate(root, source, destinations):
    root = Path(root).resolve(strict=True)
    state = root / '.state'
    source = Path(source).resolve(strict=True)
    if any(Path(os.path.abspath(value)) == state for value in destinations):
        raise ValueError('state root cannot be a destination')
    if any('..' in Path(value).parts for value in destinations):
        raise ValueError('parent traversal is not allowed')
    checked = []
    for value in [state, *map(Path, destinations)]:
        path = Path(os.path.abspath(value))
        for ancestor in [path, *path.parents]:
            if ancestor == root:
                break
            if ancestor.is_symlink():
                raise ValueError(f'symlink ancestor: {ancestor}')
        physical = path.resolve()
        if path == state:
            if physical != state:
                raise ValueError(f'state escape: {path}')
            continue
        if state not in physical.parents:
            raise ValueError(f'outside physical worktree .state: {path}')
        if physical == source or source in physical.parents or physical in source.parents:
            raise ValueError(f'overlaps SDK source: {path}')
        for previous in checked:
            if physical == previous or physical in previous.parents or previous in physical.parents:
                raise ValueError(f'overlapping destinations: {path} and {previous}')
        checked.append(physical)
    return checked


if __name__ == '__main__':
    try:
        validate(sys.argv[1], sys.argv[2], sys.argv[3:])
    except (ValueError, OSError) as error:
        sys.exit(f'Unsafe overlay path: {error}')
