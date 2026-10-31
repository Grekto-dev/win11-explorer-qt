#!/usr/bin/python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Run native selection tests only on task-owned Xephyr :12.

Source the SDK first. Usage: /usr/bin/python3 tests/run_selection_x11.py
  --binary .state/build/tests/tst_explorer --artifacts .state/selection-proof
The inherited DISPLAY is used only to host Xephyr, never for injected input.
"""
import argparse
import json
import os
from pathlib import Path
import subprocess
import sys
import time


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--binary', type=Path, required=True)
    parser.add_argument('--artifacts', type=Path, required=True)
    parser.add_argument('--session', action='store_true', help=argparse.SUPPRESS)
    args = parser.parse_args()
    binary = args.binary.resolve(strict=True)
    artifacts = args.artifacts.resolve()
    artifacts.mkdir(parents=True, exist_ok=True)
    env = os.environ.copy()
    if args.session:
        from Xlib import XK, display
        from Xlib.ext import xtest
        wm = subprocess.Popen(['metacity', '--sm-disable'], env=env,
                              stdout=(artifacts / 'wm.log').open('w'), stderr=subprocess.STDOUT)
        results = []
        try:
            time.sleep(1)
            for case in ('nativeXTestSelectionGestures', 'nativeExplorerGesturesMatchWindows'):
                with (artifacts / (case + '.log')).open('w') as log:
                    try:
                        result = subprocess.run([str(binary), case], env=env,
                                                stdout=log, stderr=subprocess.STDOUT, timeout=35)
                        code = result.returncode
                    except subprocess.TimeoutExpired:
                        code = 124
                results.append({'case': case, 'exitCode': code})
                # Independent cleanup also runs if Qt is killed by the timeout.
                d = display.Display(':12')
                try:
                    xtest.fake_input(d, 5, 1)
                    for key in ('Control_L', 'Shift_L'):
                        xtest.fake_input(d, 3, d.keysym_to_keycode(XK.string_to_keysym(key)))
                    d.sync()
                finally:
                    d.close()
        finally:
            wm.terminate()
            wm.wait(timeout=5)
        (artifacts / 'runner-result.json').write_text(json.dumps(results, indent=2) + '\n')
        return int(any(row['exitCode'] for row in results))

    if Path('/tmp/.X12-lock').exists() or Path('/tmp/.X11-unix/X12').exists():
        parser.error('Display :12 is occupied; refusing to reuse it')
    for name in ('config', 'cache', 'data', 'state', 'runtime'):
        directory = artifacts / ('xdg-' + name)
        directory.mkdir(exist_ok=True, mode=0o700)
        env['XDG_' + ('RUNTIME_DIR' if name == 'runtime' else name.upper() + '_HOME')] = str(directory)
    temporary = artifacts / 'tmp'
    temporary.mkdir(exist_ok=True)
    env.update(TMPDIR=str(temporary), GIO_USE_VFS='local', GIO_USE_VOLUME_MONITOR='unix',
               QT_NO_XDG_DESKTOP_PORTAL='1')
    # No host D-Bus or accessibility bus in the isolated session.
    for name in ('DBUS_SESSION_BUS_ADDRESS', 'AT_SPI_BUS_ADDRESS', 'SESSION_MANAGER'):
        env.pop(name, None)
    with (artifacts / 'xephyr.log').open('w') as log:
        xephyr = subprocess.Popen(['Xephyr', ':12', '-screen', '1280x900', '-ac', '-nolisten', 'tcp'],
                                  env=env, stdout=log, stderr=subprocess.STDOUT)
        try:
            from Xlib import display
            for _ in range(60):
                if xephyr.poll() is not None:
                    raise RuntimeError('Xephyr exited before opening :12')
                try:
                    d = display.Display(':12')
                    d.close()
                    break
                except Exception:
                    time.sleep(0.1)
            else:
                raise RuntimeError('Timed out opening Xephyr :12')
            env.update(DISPLAY=':12', QT_QPA_PLATFORM='xcb', NO_AT_BRIDGE='1',
                       EXPLORER_ENABLE_XTEST='1', EXPLORER_TEST_ARTIFACTS=str(artifacts))
            return subprocess.run(['dbus-run-session', '--', sys.executable, str(Path(__file__).resolve()),
                                   '--session', '--binary', str(binary), '--artifacts', str(artifacts)],
                                  env=env, timeout=85).returncode
        finally:
            xephyr.terminate()
            xephyr.wait(timeout=5)


if __name__ == '__main__':
    sys.exit(main())
