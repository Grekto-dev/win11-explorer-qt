#!/usr/bin/env python3
"""Configure-only adversarial package fixtures; no real SDK or library writes."""
import os
from pathlib import Path
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]

class PrivatePackageGuards(unittest.TestCase):
    def configure(self, scenario):
        with tempfile.TemporaryDirectory(prefix='fm-config-', dir=ROOT / '.state') as tmp:
            root = Path(tmp)
            prefix, foreign = root / 'private', root / 'foreign'
            for directory in (prefix, foreign):
                (directory / 'lib').mkdir(parents=True)
                (directory / 'lib/libfm.so').write_text('fixture, not executable')
                headers = directory / 'include/libfm-qt6'
                (headers / 'core').mkdir(parents=True)
                (headers / 'libfmqt.h').touch()
                (headers / 'core/filetransferjob.h').touch()
                (headers / 'fm-qt6_export.h').touch()
            config = prefix / 'share/cmake/fm-qt6'
            config.mkdir(parents=True)
            (config / 'fm-qt6-config-version.cmake').write_text('set(PACKAGE_VERSION 2.4.0)\nset(PACKAGE_VERSION_COMPATIBLE TRUE)\n')
            properties = f'IMPORTED_CONFIGURATIONS "RELEASE;DEBUG" IMPORTED_LOCATION_RELEASE "{prefix}/lib/libfm.so" IMPORTED_LOCATION_DEBUG "{prefix}/lib/libfm.so" INTERFACE_INCLUDE_DIRECTORIES "{prefix}/include"'
            if scenario == 'generic-foreign':
                properties += f' IMPORTED_LOCATION "{foreign}/lib/libfm.so"'
            elif scenario == 'per-config-foreign':
                properties = properties.replace(f'IMPORTED_LOCATION_DEBUG "{prefix}', f'IMPORTED_LOCATION_DEBUG "{foreign}')
            elif scenario == 'symlink-library':
                (prefix / 'lib/libfm.so').unlink()
                (prefix / 'lib/libfm.so').symlink_to(foreign / 'lib/libfm.so')
            elif scenario == 'foreign-target-includes':
                properties = properties.replace(f'INTERFACE_INCLUDE_DIRECTORIES "{prefix}/include"', f'INTERFACE_INCLUDE_DIRECTORIES "{prefix}/include;{foreign}/include"')
            elif scenario == 'foreign-system-includes':
                properties += f' INTERFACE_SYSTEM_INCLUDE_DIRECTORIES "{foreign}/include"'
            elif scenario == 'symlink-headers':
                import shutil
                shutil.rmtree(prefix / 'include/libfm-qt6')
                (prefix / 'include/libfm-qt6').symlink_to(foreign / 'include/libfm-qt6')
            package_text = f'add_library(fm-qt6 SHARED IMPORTED)\nset_target_properties(fm-qt6 PROPERTIES {properties})\n'
            if scenario == 'missing-target':
                package_text = '# no imported target\n'
            (config / 'fm-qt6-config.cmake').write_text(package_text)
            if scenario == 'symlink-package':
                (foreign / 'config.cmake').write_text(package_text)
                (config / 'fm-qt6-config.cmake').unlink()
                (config / 'fm-qt6-config.cmake').symlink_to(foreign / 'config.cmake')
            module = (ROOT / 'cmake/PrivateLibfm.cmake').read_text()
            if os.environ.get('FM_GUARD_MUTATION'):
                start = module.index('function(_private_fm_path')
                end = module.index('endfunction()', start) + len('endfunction()')
                module = module[:start] + 'function(_private_fm_path path label)\nendfunction()' + module[end:]
            (root / 'PrivateLibfm.cmake').write_text(module)
            headers = foreign if scenario == 'foreign-explicit-headers' else prefix
            (root / 'CMakeLists.txt').write_text(f'''cmake_minimum_required(VERSION 3.18)
project(guard NONE)
set(LIBFMQT_OVERLAY_PREFIX "{prefix}")
set(LIBFMQT_SOURCE_DIR "{headers}/include/libfm-qt6")
set(LIBFMQT_INCLUDE_DIR "{headers}/include/libfm-qt6")
include("{root}/PrivateLibfm.cmake")
''')
            result = subprocess.run(['uv', 'tool', 'run', '--from', 'cmake', 'cmake', '-S', str(root), '-B', str(root / 'build')], text=True, capture_output=True)
            log = ROOT / '.state/build-safety-logs' / ('package-' + scenario + ('-mutant' if os.environ.get('FM_GUARD_MUTATION') else '') + '.log')
            log.write_text(result.stdout + result.stderr)
            return result

    def test_all_imported_configurations_and_real_paths(self):
        result = self.configure('valid')
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        for scenario in ('generic-foreign', 'per-config-foreign', 'symlink-library', 'foreign-target-includes', 'foreign-system-includes', 'symlink-headers', 'missing-target', 'symlink-package', 'foreign-explicit-headers'):
            with self.subTest(scenario=scenario):
                result = self.configure(scenario)
                self.assertNotEqual(result.returncode, 0, result.stdout + result.stderr)
                self.assertIn('Private libfm:', result.stderr)

if __name__ == '__main__':
    unittest.main()
