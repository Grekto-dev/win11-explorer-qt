#!/usr/bin/python3
"""Cold-start native X11 smoke test. Requires an ISOLATED display, never :0.

SPDX-License-Identifier: GPL-2.0-or-later
"""
import argparse
import json
import os
from pathlib import Path
import subprocess
import tempfile
import time
from Xlib import X, XK, display
from Xlib.ext import xtest
from PIL import Image


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--binary', type=Path, required=True)
    args = parser.parse_args()
    target = os.environ.get('DISPLAY', '')
    if not target or target.split('.')[0] in (':0', ':1'):
        parser.error('Use an isolated nested X server such as DISPLAY=:9')
    repo = Path(__file__).resolve().parents[1]
    state = repo / '.state'
    state.mkdir(exist_ok=True)
    root = Path(tempfile.mkdtemp(prefix='e2e-', dir=state))
    for name in ('source', 'destination', 'moved', 'config', 'data', 'cache'):
        (root / name).mkdir()
    original = root / 'source/original.txt'
    original.write_text('native-engine-roundtrip\n')
    env = dict(os.environ, QT_QPA_PLATFORM='xcb', QT_STYLE_OVERRIDE='Fusion',
               XDG_CONFIG_HOME=str(root/'config'), XDG_CACHE_HOME=str(root/'cache'), XDG_DATA_HOME=str(root/'data'))
    log = (root/'app.log').open('w')
    process = subprocess.Popen([str(args.binary.resolve()), '--profile', 'e2e', str(original.parent)], env=env, stdout=log, stderr=subprocess.STDOUT)
    connection = display.Display(target)
    results = []

    def wait(predicate, label, seconds=8):
        deadline = time.monotonic()+seconds
        while time.monotonic() < deadline:
            if predicate():
                return
            if process.poll() is not None:
                raise AssertionError(f'app exited: {process.returncode}; see {root}/app.log')
            time.sleep(.05)
        raise AssertionError(f'timed out: {label}; artifacts: {root}')

    def find_window():
        for window in connection.screen().root.query_tree().children:
            try:
                # Window managers reparent clients; inspect one child level too.
                candidates = [window] + list(window.query_tree().children)
                for child in candidates:
                    if ('win11-explorer-qt' in tuple(s.lower() for s in (child.get_wm_class() or ()))
                            and child.get_attributes().map_state == X.IsViewable
                            and child.get_geometry().width > 300
                            and child.get_wm_name() == 'source'):
                        return child
            except Exception:
                continue
        return None

    def key(name, modifiers=()):
        codes = [connection.keysym_to_keycode(XK.string_to_keysym(m)) for m in modifiers]
        code = connection.keysym_to_keycode(XK.string_to_keysym(name))
        if not code:
            raise AssertionError(f'No keycode: {name}')
        for modifier in codes: xtest.fake_input(connection, X.KeyPress, modifier)
        xtest.fake_input(connection, X.KeyPress, code)
        xtest.fake_input(connection, X.KeyRelease, code)
        for modifier in reversed(codes): xtest.fake_input(connection, X.KeyRelease, modifier)
        connection.sync()
        # libfm-qt deliberately coalesces selection changes for 200 ms.
        time.sleep(.30)

    mapping = connection.get_keyboard_mapping(connection.display.info.min_keycode,
                                               connection.display.info.max_keycode-connection.display.info.min_keycode+1)
    def type_ascii(text):
        shift = connection.keysym_to_keycode(XK.string_to_keysym('Shift_L'))
        for char in text:
            choice = next(((i+connection.display.info.min_keycode, level) for i, syms in enumerate(mapping) for level in (0,1) if level<len(syms) and syms[level] == ord(char)), None)
            if choice is None:
                raise AssertionError(f'Unsupported keyboard character: {char!r}')
            code, level = choice
            if level: xtest.fake_input(connection,X.KeyPress,shift)
            xtest.fake_input(connection,X.KeyPress,code); xtest.fake_input(connection,X.KeyRelease,code)
            if level: xtest.fake_input(connection,X.KeyRelease,shift)
        connection.sync()
        time.sleep(.15)

    def navigate(path):
        key('l', ('Control_L',)); time.sleep(.15)
        key('a', ('Control_L',)); type_ascii(str(path)); key('Return')
        wait(lambda: window.get_wm_name() == path.name, f'navigate {path.name}')
        time.sleep(.3)

    def capture(name):
        try:
            image = connection.screen().root.get_image(0,0,connection.screen().width_in_pixels,connection.screen().height_in_pixels,X.ZPixmap,0xffffffff)
            Image.frombytes('RGB',(connection.screen().width_in_pixels,connection.screen().height_in_pixels),image.data,'raw','BGRX').save(root/f'{name}.png')
        except Exception:
            pass

    def click(x,y):
        xtest.fake_input(connection,X.MotionNotify,x=x,y=y)
        xtest.fake_input(connection,X.ButtonPress,1); xtest.fake_input(connection,X.ButtonRelease,1)
        connection.sync(); time.sleep(.15)

    def drag(x,y,dx,dy):
        xtest.fake_input(connection,X.MotionNotify,x=x,y=y)
        xtest.fake_input(connection,X.ButtonPress,1)
        connection.sync(); time.sleep(.12)
        for step in range(1,11):
            xtest.fake_input(connection,X.MotionNotify,x=x+dx*step//10,y=y+dy*step//10)
            connection.sync(); time.sleep(.03)
        xtest.fake_input(connection,X.ButtonRelease,1)
        connection.sync(); time.sleep(.25)

    try:
        wait(lambda: find_window() is not None,'native window')
        window = find_window()
        assert window is not None
        window.configure(x=40,y=40,width=1104,height=642)
        window.set_input_focus(X.RevertToParent,X.CurrentTime)
        connection.sync(); time.sleep(.6)
        # Use real XTEST button events on the rebuilt + control.
        # Xlib's receiver is the destination: translate client (0,0) to root.
        point = connection.screen().root.translate_coords(window,0,0)
        wx, wy = point.x, point.y
        xtest.fake_input(connection,X.MotionNotify,x=wx+266,y=wy+25)
        xtest.fake_input(connection,X.ButtonPress,1); xtest.fake_input(connection,X.ButtonRelease,1)
        connection.sync(); time.sleep(.3)
        key('w',('Control_L',))  # closes the new tab, not the window
        time.sleep(.2)
        if process.poll() is not None:
            raise AssertionError('new-tab click did not open a tab before Ctrl+W')
        results.append('native new-tab button + Ctrl+W')
        click(wx+300,wy+250)
        key('a',('Control_L',)); key('F2'); time.sleep(.3)
        capture('rename-editor')
        key('a',('Control_L',)); type_ascii('renamed.txt'); key('Return')
        renamed = original.with_name('renamed.txt')
        wait(renamed.exists,'inline rename')
        results.append('F2 inline rename')
        key('a',('Control_L',)); key('c',('Control_L',))
        navigate(root/'destination'); key('v',('Control_L',))
        copied = root/'destination/renamed.txt'
        wait(copied.exists,'copy/paste')
        assert copied.read_text() == renamed.read_text()
        results.append('copy/paste with content equality')
        navigate(root/'source'); key('a',('Control_L',)); key('x',('Control_L',))
        navigate(root/'moved'); key('v',('Control_L',))
        moved = root/'moved/renamed.txt'
        wait(lambda: moved.exists() and not renamed.exists(),'cut/paste')
        results.append('cut/paste with source removal')
        key('n',('Control_L','Shift_L')); time.sleep(.3)
        type_ascii('created'); key('Return')
        wait((root/'moved/created').is_dir,'new folder')
        results.append('Ctrl+Shift+N creates a real folder')
        before = connection.screen().root.translate_coords(window,0,0)
        drag(before.x+500,before.y+20,40,20)
        wait(lambda: abs(connection.screen().root.translate_coords(window,0,0).x-before.x)>15,'titlebar system move')
        results.append('client-drawn titlebar native move')
        before = connection.screen().root.translate_coords(window,0,0)
        size = window.get_geometry()
        drag(before.x+size.width-2,before.y+size.height-2,40,25)
        wait(lambda: window.get_geometry().width>size.width+15 and window.get_geometry().height>size.height+10,'native corner resize')
        results.append('client-drawn frame native resize')
        image = window.get_image(0,0,window.get_geometry().width,window.get_geometry().height,X.ZPixmap,0xffffffff)
        Image.frombytes('RGB',(window.get_geometry().width,window.get_geometry().height),image.data,'raw','BGRX').save(root/'native-window.png')
        report = {'passed':True,'display':target,'checks':results,'artifact_directory':str(root)}
        (root/'result.json').write_text(json.dumps(report,indent=2)+'\n')
        print(json.dumps(report,indent=2))
    finally:
        capture('last-desktop')
        if process.poll() is None:
            process.terminate()
            try: process.wait(timeout=5)
            except subprocess.TimeoutExpired: process.kill(); process.wait()
        connection.close(); log.close()


if __name__ == '__main__':
    main()
