#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Prove a prefix is relocatable: copy it to a path with spaces/accents, run it with
`env -i`, a minimal PATH, a POISONED LD_LIBRARY_PATH/QT_PLUGIN_PATH and a throw-away HOME
(offscreen, private D-Bus session), first with the dev tree visible, then with the dev tree
(<repo>/.state, the SDK behind <repo>/.deps) AND the original prefix (after the copy) hidden under tmpfs
in a mount namespace (`unshare -rm`); the original prefix appearing in maps or strace fails the proof. Checks --version, opening a folder, where every library came from, and the
`ldd` of the xcb platform plugin under `env -i`. Absolute-path reads outside the prefix
(translations, data dirs) are listed by strace when available; they are reported, not hidden.
Real xcb execution is `scripts/verify-xcb.sh`.
Usage: verify-relocation.py PREFIX [--workdir DIR] [--log FILE]"""
import argparse, os, shutil, subprocess, sys, tempfile, time
from pathlib import Path

REPO = Path(__file__).resolve().parents[1]


def check_xcb_plugin(prefix):
    """ldd of platforms/libqxcb.so under env -i: absent plugin or any 'not found' is a failure."""
    plugin = Path(prefix) / 'plugins/platforms/libqxcb.so'
    if not plugin.is_file():
        return False, f'MISSING {plugin}: the prefix cannot open a window on X11'
    r = subprocess.run(['env', '-i', 'PATH=/usr/bin:/bin', 'ldd', str(plugin)], text=True, capture_output=True)
    missing = [l.split()[0] for l in r.stdout.splitlines() if 'not found' in l]
    leaked = [l for l in r.stdout.splitlines() if '/.deps/' in l or '/.state/' in l]
    ok = r.returncode == 0 and not missing and not leaked
    return ok, f'ldd(libqxcb.so) env -i: rc={r.returncode} missing={missing} dev-tree={len(leaked)}'


def hidden_paths(src=None):
    """Directories covered by tmpfs in the private mount namespace: the dev tree and, after the copy was made,
    the ORIGINAL prefix `src` (so the moved copy cannot be silently reading it)."""
    out = []
    for cand in (REPO / '.state', (REPO / '.deps').resolve()):
        if cand.is_dir():
            out.append(str(cand))
    if src is not None:
        out.append(str(Path(src).resolve()))
    return out


def in_hidden(path, hidden):
    rp = str(Path(path).resolve())
    return any(rp == h or rp.startswith(h + '/') for h in hidden)


def overlaps_hidden(path, hidden):
    """True if `path` is inside, equal to or contains one of the hidden directories."""
    rp = str(Path(path).resolve())
    return any(rp == h or rp.startswith(h + '/') or h.startswith(rp.rstrip('/') + '/') for h in hidden)


def _under(path, src):
    src = str(src).rstrip('/')
    return path == src or path.startswith(src + '/')


def src_in_maps(maps, src):
    """Mapped files that come from the original prefix."""
    return [m for m in maps if _under(m, src)]


def src_in_strace(text, src):
    """Every path under the original prefix that the app touched (success or ENOENT), sorted, unique."""
    return sorted({w for line in text.splitlines() for w in line.split('"')[1::2] if w.startswith('/') and _under(w, src)})


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('prefix')
    ap.add_argument('--workdir')
    ap.add_argument('--log')
    a = ap.parse_args()
    src = Path(a.prefix).resolve()
    work = Path(a.workdir or tempfile.mkdtemp(prefix='reloc-')).resolve()
    work.mkdir(parents=True, exist_ok=True)
    hidden = hidden_paths()
    if in_hidden(work, hidden) or in_hidden(src, hidden):
        sys.exit(f'error: workdir/prefix live inside the dev tree that this proof hides: {hidden}')
    if overlaps_hidden(work, [str(src)]):
        sys.exit(f'error: workdir {work} overlaps the original prefix {src}, which this proof hides after copying')
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
    xok, xreport = check_xcb_plugin(moved)
    say('#', xreport)
    ok &= xok
    say(f'# moved prefix: {moved}')
    say(f'# original prefix {src}: copied; it is hidden under tmpfs for the run below and must not appear in maps/strace')
    r = subprocess.run([launcher, '--version'], env=env, text=True, capture_output=True, timeout=60)
    say(f'--version rc={r.returncode} out={r.stdout.strip()!r} err={r.stderr.strip()[:200]!r}')
    ok &= r.returncode == 0 and 'win11-explorer-qt' in r.stdout
    # LD_DEBUG=libs: where each library was found (init lines only)
    bundled_dev = [str(d) for d in (REPO / '.deps/Qt/6.8.3/gcc_64/lib', REPO / '.state/libfm-overlay-prefix/lib', REPO / '.deps/prefix/lib')]
    dev_ld = ':'.join(['/nonexistent-poison'] + bundled_dev)
    say(f'# LD_LIBRARY_PATH poisoned with the dev copies of the BUNDLED libraries (Qt, ICU, libfm-qt): {dev_ld}')
    r = subprocess.run([launcher, '--version'], env=dict(env, LD_DEBUG='libs', LD_LIBRARY_PATH=dev_ld), text=True, capture_output=True, timeout=60)
    found = [l.split('calling init:')[1].strip() for l in r.stderr.splitlines() if 'calling init:' in l]
    say(f'# LD_DEBUG=libs: {len(found)} libraries initialised for --version')
    for f in found:
        say('  ld.so:', f)
    # informational: a dev sysroot on LD_LIBRARY_PATH also shadows NON-bundled system libs (glib...); DT_RPATH cannot prevent that.
    r2 = subprocess.run([launcher, '--version'], env=dict(env, LD_DEBUG='libs', LD_LIBRARY_PATH=dev_ld + ':' + str(REPO / '.deps/sysroot/usr/lib/x86_64-linux-gnu')),
                        text=True, capture_output=True, timeout=60)
    shadow = [l for l in r2.stderr.splitlines() if 'calling init:' in l and 'sysroot' in l]
    say(f'# informational (known limit): with the dev SYSROOT on LD_LIBRARY_PATH {len(shadow)} system libs (glib etc.) are shadowed')
    bad = [f for f in found if '/.deps' in f or '/.state' in f or 'sysroot' in f or _under(f, src)]
    say(f'# libs from dev tree: {len(bad)}')
    ok &= not bad and bool(found)
    # run the app: primary opens the folder, second instance -n, then quit
    inner = f'''
import os, subprocess, sys, time
launcher, folder = {launcher!r}, {str(fixture)!r}
env = dict(os.environ)
# D-03: start from a polluted shell; the process must not carry foreign Qt paths.
poison = dict(env, QT_PLUGIN_PATH='/nonexistent/plugins', QT_QPA_PLATFORM_PLUGIN_PATH='/nonexistent/platforms', LD_LIBRARY_PATH='/nonexistent-poison')
p = subprocess.Popen([launcher, folder], env=poison, stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True)
time.sleep(4)
alive = p.poll() is None
print('primary alive after 4s:', alive)
time.sleep(0)
maps = open('/proc/%d/maps' % p.pid).read() if alive else ''
penv = dict(kv.split('=', 1) for kv in open('/proc/%d/environ' % p.pid).read().split(chr(0)) if '=' in kv) if alive else {{}}
leak = sorted(k for k in penv if k in ('QT_PLUGIN_PATH', 'QT_QPA_PLATFORM_PLUGIN_PATH', 'QT_STYLE_OVERRIDE'))
print('foreign/dev env vars in the app process:', leak)
print('app process env keys:', sorted(penv))
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
import shutil as _sh
str_log = os.environ.get('RELOC_STRACE_LOG')
if str_log and _sh.which('strace'):
    subprocess.run(['strace', '-f', '-qq', '-e', 'trace=openat,access,statx,newfstatat', '-o', str_log, 'timeout', '4', launcher, folder], env=poison, capture_output=True, timeout=30)
'''
    strace_log = work / 'strace.log'
    env['RELOC_STRACE_LOG'] = str(strace_log)
    hidden = hidden_paths(src)  # dev tree + the original prefix (the copy was made above, before the namespace)
    hide = ' && '.join(f'mount -t tmpfs none {h!r}' for h in hidden) or 'true'
    say(f'# dev tree AND original prefix hidden under tmpfs in a mount namespace: {hidden}')
    r = subprocess.run(['unshare', '-rm', '/bin/sh', '-c', hide + ' && exec "$@"', 'sh',
                        'dbus-run-session', '--', '/usr/bin/python3', '-c', inner], env=env, text=True, capture_output=True, timeout=180)
    say(r.stdout.rstrip())
    if r.stderr.strip():
        say('(dbus-run-session stderr)', r.stderr.strip()[:300])
    out = r.stdout
    ok &= 'primary alive after 4s: True' in out and 'primary exit: 0' in out and 'quit rc: 0' in out
    ok &= 'second instance -n rc: 0' in out
    ok &= 'foreign/dev env vars in the app process: []' in out
    maps = [l.split('maps:')[1].strip() for l in out.splitlines() if l.strip().startswith('maps:')]
    ok &= all('/.deps' not in m and '/.state' not in m and 'sysroot' not in m for m in maps)
    ok &= any(str(moved) in m for m in maps)
    from_src = src_in_maps(maps, src)
    say(f'# mapped files from the ORIGINAL prefix: {len(from_src)}')
    for m in from_src[:10]:
        say('  from original prefix:', m)
    ok &= not from_src
    if strace_log.exists():
        seen = sorted({w for l in strace_log.read_text(errors='replace').splitlines() for w in l.split('"')[1::2]
                       if w.startswith('/') and (str(REPO) in w or '/.deps' in w or '/.state' in w) and 'ENOENT' in l})
        say(f'# absolute paths outside the prefix that the app TRIED to open (dev tree hidden, so they failed): {len(seen)}')
        for w in seen[:30]:
            say('  tried:', w)
        hits = src_in_strace(strace_log.read_text(errors='replace'), src)
        say(f'# strace: paths under the ORIGINAL prefix touched by the app: {len(hits)}')
        for w in hits[:30]:
            say('  original prefix touched:', w)
        ok &= not hits
    else:
        say('# strace unavailable: absolute-path reads outside the prefix were NOT checked')
    say('RESULT:', 'PASS' if ok else 'FAIL')
    if a.log:
        Path(a.log).write_text('\n'.join(log) + '\n')
    shutil.rmtree(work) if not a.workdir else None
    sys.exit(0 if ok else 1)


if __name__ == '__main__':
    main()
