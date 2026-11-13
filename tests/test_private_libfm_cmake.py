#!/usr/bin/env python3
"""Real configure regression: stale fm-qt6_DIR must never select the SDK ABI."""
from pathlib import Path
import os
import subprocess
import unittest

ROOT = Path(__file__).resolve().parents[1]
SDK = (ROOT / '.deps/prefix').resolve()

class PrivateLibfm(unittest.TestCase):
    def test_stale_cache_then_prefix_switch(self):
        (ROOT / '.state/build-safety-logs').mkdir(parents=True, exist_ok=True)
        overlay = Path(os.environ.get('L4_TEST_PREFIX', ROOT / '.state/libfm-overlay-prefix'))
        alternate = ROOT / '.state/cache-test-prefix'
        build = ROOT / '.state/cache-regression-build'
        # Full copied prefix so real imported per-config paths are exercised.
        import shutil
        if alternate.exists():
            shutil.rmtree(alternate)
        shutil.copytree(overlay, alternate, symlinks=True)
        probe = ROOT / '.state/cache-probe-source'
        probe.mkdir(exist_ok=True)
        (probe / 'CMakeLists.txt').write_text(f'''cmake_minimum_required(VERSION 3.18)
project(cache-probe CXX)
find_package(Qt6 6.6 REQUIRED COMPONENTS Widgets)
include("{ROOT}/cmake/PrivateLibfm.cmake")
add_executable(cache-probe main.cpp)
target_include_directories(cache-probe PRIVATE "${{LIBFMQT_INCLUDE_DIR}}")
target_link_libraries(cache-probe PRIVATE fm-qt6 ${{CMAKE_DL_LIBS}})
''')
        (probe / 'main.cpp').write_text('''#include <fileoperation.h>
#include <dlfcn.h>
#include <iostream>
int main() {
    Fm::FileOperation::setDialogFactory(nullptr);
    Dl_info loaded{};
    if (!dladdr(reinterpret_cast<void*>(&Fm::FileOperation::setDialogFactory), &loaded)) return 2;
    std::cout << loaded.dli_fname << std::endl;
}
''')
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
            # Reuse a compiled consumer cache as well. The new overlay-only API
            # must link, and dladdr must identify this exact prefix at runtime.
            probe_build = ROOT / '.state/cache-probe-build'
            probe_command = f'''set -euo pipefail
source "$PWD/.deps/env.sh"
cmake -S "{probe}" -B "{probe_build}" -GNinja -DCMAKE_MAKE_PROGRAM="$NINJA_BIN" \\
 -DCMAKE_PREFIX_PATH="{prefix};$SDK_PREFIX;$QT_ROOT" \\
 -Dfm-qt6_DIR="{stale}/share/cmake/fm-qt6" \\
 -DLIBFMQT_OVERLAY_PREFIX="{prefix}" \\
 -DLIBFMQT_INCLUDE_DIR="{prefix}/include/libfm-qt6" \\
 -DLIBFMQT_SOURCE_DIR="{prefix}/include/libfm-qt6" \\
 -DXKB_INCLUDE_DIR="$SYSROOT/usr/include" \\
 -DXKB_LIBRARY="$SYSROOT/usr/lib/x86_64-linux-gnu/libxkbcommon.so"
cmake --build "{probe_build}" --parallel 2
LD_LIBRARY_PATH="{prefix}/lib:$LD_LIBRARY_PATH" "{probe_build}/cache-probe"
'''
            result = subprocess.run(['bash', '-c', probe_command], cwd=ROOT, text=True, capture_output=True)
            (ROOT / '.state/build-safety-logs' / ('linked-probe-' + prefix.name + '.log')).write_text(result.stdout + result.stderr)
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
            loaded = Path(result.stdout.strip().splitlines()[-1]).resolve()
            self.assertEqual(loaded, (prefix / 'lib/libfm-qt6.so.17.0.0').resolve())
            probe_cache = (probe_build / 'CMakeCache.txt').read_text()
            self.assertIn('fm-qt6_DIR:PATH=' + str(prefix / 'share/cmake/fm-qt6'), probe_cache)

if __name__ == '__main__':
    unittest.main()
