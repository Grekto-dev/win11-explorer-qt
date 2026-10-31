#!/usr/bin/env python3
"""Timestamped clean-screen capture and QEMU input helper for Explorer studies."""
from __future__ import annotations

import argparse
import json
import subprocess
import time
from datetime import datetime, timezone
from pathlib import Path

WIDTH, HEIGHT = 1440, 900
DOMAIN = 'windows11'


def abs_value(position: int, extent: int) -> int:
    if extent < 2 or not 0 <= position < extent:
        raise ValueError('position must be within an extent of at least two pixels')
    return round(position * 32767 / (extent - 1))


def move_events(x: int, y: int, width: int = WIDTH, height: int = HEIGHT) -> list[dict]:
    return [
        {'type': 'abs', 'data': {'axis': 'x', 'value': abs_value(x, width)}},
        {'type': 'abs', 'data': {'axis': 'y', 'value': abs_value(y, height)}},
    ]


def click_events() -> list[dict]:
    return [
        {'type': 'btn', 'data': {'down': True, 'button': 'left'}},
        {'type': 'btn', 'data': {'down': False, 'button': 'left'}},
    ]


def send(events: list[dict]) -> None:
    payload = {'execute': 'input-send-event', 'arguments': {'events': events}}
    subprocess.run(['virsh', '-c', 'qemu:///system', 'qemu-monitor-command', DOMAIN,
                    json.dumps(payload)], check=True, capture_output=True, text=True)


def screenshot(path: Path) -> None:
    subprocess.run(['virsh', '-c', 'qemu:///system', 'screenshot', DOMAIN, str(path)],
                   check=True, capture_output=True, text=True)


def record(out: Path, actions: list[dict]) -> list[dict]:
    out.mkdir(parents=True, exist_ok=True)
    log = []
    for i, action in enumerate(actions):
        start = time.monotonic_ns()
        action['utc_started'] = datetime.now(timezone.utc).isoformat()
        if action['kind'] == 'move':
            send(move_events(action['x'], action['y']))
        elif action['kind'] == 'click':
            send(move_events(action['x'], action['y']))
            time.sleep(.08)
            send([click_events()[0]])
            time.sleep(.12)
            send([click_events()[1]])
        elif action['kind'] == 'wait':
            time.sleep(action['seconds'])
        elif action['kind'] == 'key':
            subprocess.run(['virsh', '-c', 'qemu:///system', 'send-key', DOMAIN, *action['keys']], check=True)
        else:
            raise ValueError(f"unknown action kind: {action['kind']}")
        time.sleep(action.get('settle', .5))
        path = out / f"{i:02d}_{action['id']}.png"
        screenshot(path)
        action['monotonic_start_ns'] = start
        action['monotonic_elapsed_ms'] = round((time.monotonic_ns() - start) / 1e6, 3)
        action['screenshot'] = path.name
        action['screenshot_utc'] = datetime.now(timezone.utc).isoformat()
        log.append(action.copy())
    (out / 'input-log.json').write_text(json.dumps(log, ensure_ascii=False, indent=2) + '\n')
    return log


def main() -> None:
    p = argparse.ArgumentParser()
    p.add_argument('--out', type=Path, required=True)
    p.add_argument('actions_json', type=Path)
    args = p.parse_args()
    actions = json.loads(args.actions_json.read_text())
    print(json.dumps(record(args.out, actions), ensure_ascii=False, indent=2))


if __name__ == '__main__':
    main()
