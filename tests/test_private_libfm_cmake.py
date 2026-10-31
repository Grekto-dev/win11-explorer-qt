#!/usr/bin/env python3
"""Real configure regression: stale fm-qt6_DIR must never select the SDK ABI."""
from pathlib import Path
import os
import subprocess
import unittest

ROOT = Path(__file__).resolve().parents[1]
SDK = Path('/home/pedro/win11-explorer-qt/.deps/prefix')

class PrivateLibfm(unittest.TestCase):
    def test_stale_cache_then_prefix_switch(self):
        overlay = Path(os.environ.get('L4_TEST_PREFIX', ROOT / '.state/libfm-overlay-prefix'))
        alternate = ROOT / '.state/cache-test-prefix'
        build = ROOT / '.state/cache-regression-build'
        # Full copied prefix so real imported per-config paths are exercised.
        import shutil
        if alternate.exists():
            shutil.rmtree(alternate)
        shutil.copytree(overlay, alternate, symlinks=True)
        for prefix, stale in [(overlay, SDK), (alternate, overlay)]:
            command = f'''source "$PWD/.deps/env.sh"
cmake -S "$PWD" -B "{build}" -GNinja -DCMAKE_MAKE_PROGRAM="$NINJA_BIN" \\
 -DCMAKE_PREFIX_PATH="{prefix};$SDK_PREFIX;$QT_ROOT" \\
 -Dfm-qt6_DIR="{stale}/share/cmake/fm-qt6" \\
 -DLIBFMQT_OVERLAY_PREFIX="{prefix}" \\
 -DLIBFMQT_INCLUDE_DIR="{prefix}/include/libfm-qt6" \\
 -DLIBFMQT_SOURCE_DIR="{prefix}/include/libfm-qt6" \\
 -DCMAKE_MODULE_PATH="$SDK_PREFIX/share/cmake/lxqt2-build-tools/find-modules" \\
 -DXKB_INCLUDE_DIR="$SYSROOT/usr/include" \\
 -DXKB_LIBRARY="$SYSROOT/usr/lib/x86_64-linux-gnu/libxkbcommon.so" \\
 -DPython3_EXECUTABLE=/usr/bin/python3
'''
            result = subprocess.run(['bash', '-c', command], cwd=ROOT, text=True, capture_output=True)
            (ROOT / '.state/build-safety-logs' / ('configure-' + prefix.name + '.log')).write_text(result.stdout + result.stderr)
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
            cache = (build / 'CMakeCache.txt').read_text()
            self.assertIn('fm-qt6_DIR:PATH=' + str(prefix / 'share/cmake/fm-qt6'), cache)
            evidence = (build / 'private-libfm-selection.txt').read_text()
            self.assertIn(str(prefix / 'lib/libfm-qt6.so.17.0.0'), evidence)
            self.assertNotIn(str(SDK / 'lib/libfm-qt6'), evidence)

if __name__ == '__main__':
    unittest.main()
