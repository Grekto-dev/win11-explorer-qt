#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Prove a prefix is relocatable: copy it to a path with spaces/accents, run it with
`env -i`, a minimal PATH and a throw-away HOME (offscreen, private D-Bus session),
check --version, open a folder, and list where every loaded library came from.
Usage: verify-relocation.py PREFIX [--workdir DIR] [--log FILE]"""
import argparse, os, shutil, subprocess, sys, tempfile, time
from pathlib import Path


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('prefix')
    ap.add_argument('--workdir')
    ap.add_argument('--log')
    a = ap.parse_args()
    src = Path(a.prefix).resolve()
    work = Path(a.workdir or tempfile.mkdtemp(prefix='reloc-'))
    work.mkdir(parents=True, exist_ok=True)
    moved = work / 'instalação com espaço' / 'Win11 Explorer'
    shutil.copytree(src, moved, symlinks=True)
    home = work / 'home'
    for d in ('', '.config', '.local/share', '.cache', '.local/state', 'run'):
        (home / d).mkdir(parents=True, exist_ok=True)
    os.chmod(home / 'run', 0o700)
    fixture = work / 'pasta de teste'
    fixture.mkdir(exist_ok=True)
    (fixture / 'arquivo.txt').write_text('x')
    env = {'PATH': '/usr/bin:/bin', 'HOME': str(home), 'XDG_CONFIG_HOME': str(home / '.config'),
           'XDG_DATA_HOME': str(home / '.local/share'), 'XDG_CACHE_HOME': str(home / '.cache'),
           'XDG_STATE_HOME': str(home / '.local/state'), 'XDG_RUNTIME_DIR': str(home / 'run'),
           'QT_QPA_PLATFORM': 'offscreen', 'LANG': 'C.UTF-8'}
    launcher = str(moved / 'bin/win11-explorer-qt')
    log = []
    def say(*x):
        line = ' '.join(str(i) for i in x)
        print(line)
        log.append(line)
    ok = True
    say(f'# moved prefix: {moved}')
    say(f'# original prefix no longer used: {src}')
    r = subprocess.run([launcher, '--version'], env=env, text=True, capture_output=True, timeout=60)
    say(f'--version rc={r.returncode} out={r.stdout.strip()!r} err={r.stderr.strip()[:200]!r}')
    ok &= r.returncode == 0 and 'win11-explorer-qt' in r.stdout
    # LD_DEBUG=libs: where each library was found (init lines only)
    r = subprocess.run([launcher, '--version'], env=dict(env, LD_DEBUG='libs'), text=True, capture_output=True, timeout=60)
    found = [l.split('calling init:')[1].strip() for l in r.stderr.splitlines() if 'calling init:' in l]
    say(f'# LD_DEBUG=libs: {len(found)} libraries initialised for --version')
    for f in found:
        say('  ld.so:', f)
    bad = [f for f in found if '/.deps' in f or '/.state' in f or 'sysroot' in f]
    say(f'# libs from dev tree: {len(bad)}')
    ok &= not bad and bool(found)
    # run the app: primary opens the folder, second instance -n, then quit
    inner = f'''
import os, subprocess, sys, time
launcher, folder = {launcher!r}, {str(fixture)!r}
env = dict(os.environ)
p = subprocess.Popen([launcher, folder], env=env, stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True)
time.sleep(4)
alive = p.poll() is None
print('primary alive after 4s:', alive)
maps = open('/proc/%d/maps' % p.pid).read() if alive else ''
exe = os.readlink('/proc/%d/exe' % p.pid) if alive else ''
libs = sorted({{l.split(None, 5)[5].replace(' (deleted)', '') for l in maps.splitlines() if len(l.split(None, 5)) == 6 and '.so' in l.split(None, 5)[5]}})
print('exe:', exe)
for l in libs: print('  maps:', l)
r = subprocess.run([launcher, '-n', folder], env=env, capture_output=True, text=True, timeout=40)
print('second instance -n rc:', r.returncode)
q = subprocess.run([launcher, '-q'], env=env, capture_output=True, text=True, timeout=40)
print('quit rc:', q.returncode)
try:
    rc = p.wait(timeout=20)
except subprocess.TimeoutExpired:
    p.kill(); rc = 'killed'
print('primary exit:', rc)
print('stderr tail:', p.stderr.read()[-600:].replace(chr(10), ' | '))
'''
    r = subprocess.run(['dbus-run-session', '--', '/usr/bin/python3', '-c', inner], env=env, text=True, capture_output=True, timeout=120)
    say(r.stdout.rstrip())
    if r.stderr.strip():
        say('(dbus-run-session stderr)', r.stderr.strip()[:300])
    out = r.stdout
    ok &= 'primary alive after 4s: True' in out and 'primary exit: 0' in out and 'quit rc: 0' in out
    ok &= 'second instance -n rc: 0' in out
    maps = [l.split('maps:')[1].strip() for l in out.splitlines() if l.strip().startswith('maps:')]
    ok &= all('/.deps' not in m and '/.state' not in m and 'sysroot' not in m for m in maps)
    ok &= any(str(moved) in m for m in maps)
    say('RESULT:', 'PASS' if ok else 'FAIL')
    if a.log:
        Path(a.log).write_text('\n'.join(log) + '\n')
    shutil.rmtree(work) if not a.workdir else None
    sys.exit(0 if ok else 1)


main()
