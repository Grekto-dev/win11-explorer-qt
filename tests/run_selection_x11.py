#!/usr/bin/python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Run XTEST only on an exclusively reserved, proven child Xephyr.

Inherited DISPLAY only hosts the nested server; no input is sent to it.
Not a sandbox against a malicious process with the same UID.
"""
import argparse
import json
import os
from pathlib import Path
import subprocess
import sys
import time
from selection_ownership import (reserve, check_free, process_identity,
                                 verify_environment, display_number)


def stop(process):
    if process.poll() is None:
        process.terminate()
        try:
            process.wait(timeout=5)
        except subprocess.TimeoutExpired:
            process.kill()
            process.wait()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--binary', type=Path, required=True)
    parser.add_argument('--artifacts', type=Path, required=True)
    parser.add_argument('--display', default=os.environ.get('EXPLORER_XTEST_DISPLAY', ':18'))
    parser.add_argument('--case', action='append')
    parser.add_argument('--session', action='store_true', help=argparse.SUPPRESS)
    args = parser.parse_args()
    binary = args.binary.resolve(strict=True)
    artifacts = args.artifacts.resolve()
    env = os.environ.copy()
    try:
        display_number(args.display)
        if args.session:
            verify_environment(env)  # Before WM or any input connection.
            from Xlib import XK, display
            from Xlib.ext import xtest
            with (artifacts / 'wm.log').open('w') as log:
                wm = subprocess.Popen(['metacity', '--sm-disable'], env=env,
                                      stdout=log, stderr=subprocess.STDOUT)
                results = []
                try:
                    time.sleep(1)
                    for case in args.case or ('nativeXTestSelectionGestures', 'nativeExplorerGesturesMatchWindows', 'nativeInputFailureCleanup'):
                        verify_environment(env)
                        with (artifacts / (case + '.log')).open('w') as case_log:
                            try:
                                result = subprocess.run([str(binary), case], env=env,
                                                        stdout=case_log, stderr=subprocess.STDOUT, timeout=40)
                                code = result.returncode
                            except subprocess.TimeoutExpired:
                                code = 124
                        results.append({'case': case, 'exitCode': code})
                        # Emergency cleanup is additional to the in-process guards.
                        verify_environment(env)
                        d = display.Display(args.display)
                        try:
                            xtest.fake_input(d, 5, 1)
                            for key in ('Control_L', 'Shift_L'):
                                xtest.fake_input(d, 3, d.keysym_to_keycode(XK.string_to_keysym(key)))
                            d.sync()
                        finally:
                            d.close()
                        if code:
                            break  # Never follow a failed gesture with another case.
                finally:
                    stop(wm)
            (artifacts / 'runner-result.json').write_text(json.dumps(results, indent=2) + '\n')
            return int(any(row['exitCode'] for row in results))

        # Reserve in inherited COMMON scratch, before per-run TMPDIR changes.
        scratch = Path(os.environ.get('TMPDIR', str(Path.home() / '.cache/explorer-tests'))).resolve()
        scratch.mkdir(parents=True, exist_ok=True)
        with reserve(args.display, scratch):
            check_free(args.display)
            artifacts.mkdir(parents=True, exist_ok=True)
            for name in ('config', 'cache', 'data', 'state', 'runtime'):
                directory = artifacts / ('xdg-' + name)
                directory.mkdir(exist_ok=True, mode=0o700)
                env['XDG_' + ('RUNTIME_DIR' if name == 'runtime' else name.upper() + '_HOME')] = str(directory)
            temporary = artifacts / 'tmp'
            temporary.mkdir(exist_ok=True)
            env.update(TMPDIR=str(temporary), GIO_USE_VFS='local', GIO_USE_VOLUME_MONITOR='unix',
                       QT_NO_XDG_DESKTOP_PORTAL='1')
            for name in ('DBUS_SESSION_BUS_ADDRESS', 'AT_SPI_BUS_ADDRESS', 'SESSION_MANAGER'):
                env.pop(name, None)
            with (artifacts / 'xephyr.log').open('w') as log:
                xephyr = subprocess.Popen(['Xephyr', args.display, '-screen', '1280x900', '-ac', '-nolisten', 'tcp'],
                                          env=env, stdout=log, stderr=subprocess.STDOUT)
                try:
                    env.update(EXPLORER_XSERVER_PID=str(xephyr.pid),
                               EXPLORER_XSERVER_START=process_identity(xephyr.pid)[1],
                               EXPLORER_XOWNER_PID=str(os.getpid()),
                               EXPLORER_XOWNER_START=process_identity(os.getpid())[1],
                               DISPLAY=args.display, EXPLORER_XTEST_DISPLAY=args.display,
                               QT_QPA_PLATFORM='xcb', NO_AT_BRIDGE='1', EXPLORER_ENABLE_XTEST='1',
                               EXPLORER_TEST_ARTIFACTS=str(artifacts))
                    from Xlib import display
                    for _ in range(60):
                        if xephyr.poll() is not None:
                            raise RuntimeError('Xephyr exited before opening ' + args.display)
                        try:
                            verify_environment(env)
                            d = display.Display(args.display)
                            d.close()
                            verify_environment(env)
                            break
                        except (OSError, RuntimeError):
                            time.sleep(0.1)
                    else:
                        raise RuntimeError('Timed out proving Xephyr ownership ' + args.display)
                    (artifacts / 'ownership.json').write_text(json.dumps({k:v for k,v in env.items() if k.startswith('EXPLORER_X')}, indent=2) + '\n')
                    command = ['dbus-run-session', '--', sys.executable, str(Path(__file__).resolve()),
                               '--session', '--display', args.display, '--binary', str(binary), '--artifacts', str(artifacts)]
                    for case in args.case or []:
                        command += ['--case', case]
                    return subprocess.run(command, env=env, timeout=145).returncode
                finally:
                    stop(xephyr)
    except (RuntimeError, KeyError, OSError, ValueError) as error:
        parser.error(str(error))


if __name__ == '__main__':
    sys.exit(main())
