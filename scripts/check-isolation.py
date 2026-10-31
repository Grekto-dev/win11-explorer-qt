#!/usr/bin/env python3
"""Snapshot only desktop configuration metadata; never change settings.

SPDX-License-Identifier: GPL-2.0-or-later
"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import subprocess
import sys


def snapshot():
    commands = {
        "directory_handler": ["xdg-mime", "query", "default", "inode/directory"],
        "filemanager_service_owner": ["gdbus", "call", "--session", "--dest", "org.freedesktop.DBus", "--object-path", "/org/freedesktop/DBus", "--method", "org.freedesktop.DBus.GetNameOwner", "org.freedesktop.FileManager1"],
        "applets": ["gsettings", "get", "org.cinnamon", "enabled-applets"],
        "desktop_icons": ["gsettings", "get", "org.nemo.desktop", "show-desktop-icons"],
    }
    result = {}
    for key, command in commands.items():
        proc = subprocess.run(command, text=True, capture_output=True, check=False)
        result[key] = {"status": proc.returncode, "value": proc.stdout.strip() or proc.stderr.strip()}
    for relative in (".config/mimeapps.list", ".local/share/applications/mimeapps.list", ".config/nemo/desktop-metadata"):
        path = Path.home() / relative
        result[relative] = hashlib.sha256(path.read_bytes()).hexdigest() if path.is_file() else None
    return result


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("mode", choices=("save", "verify"))
    parser.add_argument("baseline", type=Path)
    args = parser.parse_args()
    current = snapshot()
    if args.mode == "save":
        if args.baseline.exists():
            parser.error("baseline already exists; preserve the original")
        args.baseline.parent.mkdir(parents=True, exist_ok=True)
        args.baseline.write_text(json.dumps(current, indent=2, ensure_ascii=False) + "\n")
        print(f"Saved isolation baseline: {args.baseline}")
        return 0
    previous = json.loads(args.baseline.read_text())
    changed = {key: {"before": previous.get(key), "after": value} for key, value in current.items() if previous.get(key) != value}
    if changed:
        print(json.dumps(changed, indent=2, ensure_ascii=False))
        return 1
    print("PASS: directory handler, FileManager1 owner, Cinnamon applets, Nemo desktop and MIME files unchanged")
    return 0


if __name__ == "__main__":
    sys.exit(main())
