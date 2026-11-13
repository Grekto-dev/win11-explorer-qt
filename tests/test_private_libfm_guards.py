#!/usr/bin/env python3
"""Configure-only adversarial package fixtures; no real SDK or library writes."""
import os
from pathlib import Path
import subprocess
import tempfile
import unittest

from private_libfm_fixture import fixture_plan

ROOT = Path(__file__).resolve().parents[1]

class PrivatePackageGuards(unittest.TestCase):
    def configure(self, scenario, *, build_type='RelWithDebInfo', multi=False,
                  map_source='target', mapping='CUSTOM', header='core/fileoperationjob.h',
                  trace_headers=False):
        overlay = Path(os.environ.get('L4_TEST_PREFIX', ROOT / '.state/libfm-overlay-prefix')).resolve()
        with fixture_plan(ROOT, overlay, ROOT / '.state/build-safety-logs') as (root, logs):
            prefix, foreign = root / 'private', root / 'foreign'
            for directory in (prefix, foreign):
                (directory / 'lib').mkdir(parents=True)
                subprocess.run(['cc', '-shared', '-fPIC', '-x', 'c', '-',
                                '-Wl,-soname,libfm.so.17', '-o', str(directory / 'lib/libfm.so')],
                               input='int fixture(void) { return 0; }\n', text=True,
                               capture_output=True, check=True)
                (directory / 'lib/libfm.so.17').symlink_to('libfm.so')
                headers = directory / 'include/libfm-qt6'
                (headers / 'core').mkdir(parents=True)
                (headers / 'libfmqt.h').touch()
                for name in ('core/job.h', 'core/fileoperationjob.h', 'core/filetransferjob.h',
                             'core/gioptrs.h', 'core/gobjectptr.h', 'core/filepath.h',
                             'core/fileinfo.h', 'libfmqtglobals.h', 'fm-qt6_export.h',
                             'core/dependencies/helper.h', 'core/.hidden-dependency.h'):
                    (headers / name).parent.mkdir(parents=True, exist_ok=True)
                    (headers / name).touch()
            config = prefix / 'share/cmake/fm-qt6'
            config.mkdir(parents=True)
            (config / 'fm-qt6-config-version.cmake').write_text('set(PACKAGE_VERSION 2.4.0)\nset(PACKAGE_VERSION_COMPATIBLE TRUE)\n')
            properties = f'IMPORTED_CONFIGURATIONS "RELEASE;DEBUG" IMPORTED_LOCATION_RELEASE "{prefix}/lib/libfm.so" IMPORTED_LOCATION_DEBUG "{prefix}/lib/libfm.so" INTERFACE_INCLUDE_DIRECTORIES "{prefix}/include"'
            cmake_setup = ''
            cmake_args = [f'-DCMAKE_BUILD_TYPE={build_type}']
            if multi:
                ninja = subprocess.check_output(['uv', 'tool', 'run', '--from', 'ninja', '--', 'python', '-c', 'import shutil; print(shutil.which("ninja"))'], text=True).strip()
                cmake_args += ['-G', 'Ninja Multi-Config', f'-DCMAKE_MAKE_PROGRAM={ninja}',
                               f'-DCMAKE_CONFIGURATION_TYPES=Debug;{build_type}']
            if scenario.startswith('mapped-'):
                # CUSTOM deliberately absent from IMPORTED_CONFIGURATIONS.
                properties = f'IMPORTED_LOCATION "{prefix}/lib/libfm.so" IMPORTED_LOCATION_CUSTOM "{foreign}/lib/libfm.so" INTERFACE_INCLUDE_DIRECTORIES "{prefix}/include"'
                if scenario in ('mapped-internal', 'mapped-only-internal'):
                    properties = properties.replace(f'IMPORTED_LOCATION_CUSTOM "{foreign}', f'IMPORTED_LOCATION_CUSTOM "{prefix}')
                if scenario == 'mapped-only-internal':
                    properties = properties.replace(f'IMPORTED_LOCATION "{prefix}/lib/libfm.so" ', '')
                map_name = f'MAP_IMPORTED_CONFIG_{build_type.upper()}'
                if map_source == 'target':
                    properties += f' {map_name} "{mapping}"'
                elif map_source == 'default':
                    cmake_setup = f'set(CMAKE_{map_name} "{mapping}")\n'
                else:
                    cmake_args += [f'-DCMAKE_{map_name}:STRING={mapping}']
            if scenario == 'external-soname':
                (prefix / 'lib/libfm.so.17').unlink()
                (prefix / 'lib/libfm.so.17').symlink_to(foreign / 'lib/libfm.so')
            elif scenario == 'different-soname':
                import shutil
                shutil.copy2(prefix / 'lib/libfm.so', prefix / 'lib/other.so')
                (prefix / 'lib/libfm.so.17').unlink()
                (prefix / 'lib/libfm.so.17').symlink_to('other.so')
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
            if scenario == 'symlink-header-file':
                installed = prefix / 'include/libfm-qt6' / header
                installed.unlink()
                installed.symlink_to(foreign / 'include/libfm-qt6' / header)
            elif scenario == 'symlink-header-directory':
                import shutil
                installed = prefix / 'include/libfm-qt6/core/dependencies'
                shutil.rmtree(installed)
                installed.symlink_to(foreign / 'include/libfm-qt6/core/dependencies')
                (foreign / 'include/libfm-qt6/core/dependencies/EXTERNAL_TREE_MUST_NOT_BE_ENUMERATED.h').touch()
            package_text = f'add_library(fm-qt6 SHARED IMPORTED)\nset_target_properties(fm-qt6 PROPERTIES {properties})\n'
            if scenario.startswith('mapped-'):
                package_text += f'get_target_property(selected fm-qt6 LOCATION_{build_type.upper()})\nmessage(STATUS "fixture-selected=${{selected}}")\n'
            if scenario == 'missing-target':
                package_text = '# no imported target\n'
            (config / 'fm-qt6-config.cmake').write_text(package_text)
            if scenario == 'symlink-package':
                (foreign / 'config.cmake').write_text(package_text)
                (config / 'fm-qt6-config.cmake').unlink()
                (config / 'fm-qt6-config.cmake').symlink_to(foreign / 'config.cmake')
            module = (ROOT / 'cmake/PrivateLibfm.cmake').read_text()
            mutation = os.environ.get('FM_GUARD_MUTATION')
            if mutation == 'map':
                start = module.index('foreach(_fm_property IN LISTS _fm_selected_properties)')
                end = module.index('endforeach()', start) + len('endforeach()')
                module = module[:start] + module[end:]
            elif mutation == 'headers':
                start = module.index('function(_private_fm_headers')
                end = module.index('endfunction()', start) + len('endfunction()')
                module = module[:start] + 'function(_private_fm_headers directory)\nendfunction()' + module[end:]
            elif mutation:
                start = module.index('function(_private_fm_path')
                end = module.index('endfunction()', start) + len('endfunction()')
                module = module[:start] + 'function(_private_fm_path path label)\nendfunction()' + module[end:]
            (root / 'PrivateLibfm.cmake').write_text(module)
            headers = foreign if scenario == 'foreign-explicit-headers' else prefix
            (root / 'CMakeLists.txt').write_text(f'''cmake_minimum_required(VERSION 3.18)
project(guard NONE)
{cmake_setup}set(LIBFMQT_OVERLAY_PREFIX "{prefix}")
set(LIBFMQT_SOURCE_DIR "{headers}/include/libfm-qt6")
set(LIBFMQT_INCLUDE_DIR "{headers}/include/libfm-qt6")
include("{root}/PrivateLibfm.cmake")
''')
            if trace_headers:
                cmake_args += ['--trace-expand', f'--trace-source={root}/PrivateLibfm.cmake']
            result = subprocess.run(['uv', 'tool', 'run', '--from', 'cmake', 'cmake', '-S', str(root), '-B', str(root / 'build'), *cmake_args], text=True, capture_output=True)
            variant = f'-{map_source}-{build_type}-multi{multi}-{mapping.replace(";", "_")}' if scenario.startswith('mapped-') else ''
            if scenario == 'symlink-header-file':
                variant = '-' + header.replace('/', '_')
            log = logs / ('package-' + scenario + variant + ('-mutant-' + mutation if mutation else '') + '.log')
            log.write_text(result.stdout + result.stderr)
            return result

    def test_soname_is_the_selected_physical_artifact(self):
        for scenario in ('external-soname', 'different-soname'):
            with self.subTest(scenario=scenario):
                result = self.configure(scenario)
                self.assertNotEqual(result.returncode, 0, result.stdout + result.stderr)
                self.assertIn('Private libfm:', result.stderr)

    def test_empty_build_type_and_unavailable_mapping(self):
        result = self.configure('valid', build_type='')
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        result = self.configure('mapped-internal', mapping='MISSING')
        self.assertNotEqual(result.returncode, 0, result.stdout + result.stderr)
        self.assertIn('no location', result.stderr)

    def test_individual_header_escapes(self):
        for header in ('core/job.h', 'core/fileoperationjob.h', 'core/filetransferjob.h',
                       'core/gioptrs.h', 'core/gobjectptr.h', 'core/filepath.h',
                       'core/fileinfo.h', 'libfmqtglobals.h', 'fm-qt6_export.h',
                       'core/dependencies/helper.h', 'core/.hidden-dependency.h'):
            with self.subTest(header=header):
                result = self.configure('symlink-header-file', header=header)
                self.assertNotEqual(result.returncode, 0, result.stdout + result.stderr)
                self.assertIn('escapes expected prefix', result.stderr)
                self.assertIn(header, result.stderr)

    def test_subdirectory_escape_rejected_before_enumeration(self):
        result = self.configure('symlink-header-directory', trace_headers=True)
        self.assertNotEqual(result.returncode, 0, result.stdout + result.stderr)
        self.assertIn('header-entry escapes expected prefix:', result.stderr)
        self.assertNotIn('EXTERNAL_TREE_MUST_NOT_BE_ENUMERATED.h', result.stderr)
        self.assertNotRegex(result.stderr, r'file\(GLOB[^\n]*(?:/foreign/|/core/dependencies)')

    def test_mapped_unadvertised_external_location(self):
        for source in ('target', 'default', 'cache'):
            for build_type, multi in (('RelWithDebInfo', False), ('Shipping', False), ('Shipping', True)):
                with self.subTest(source=source, build_type=build_type, multi=multi):
                    result = self.configure('mapped-foreign', map_source=source,
                                            build_type=build_type, multi=multi)
                    self.assertRegex(result.stdout, r'fixture-selected=.*/foreign/lib/libfm.so')
                    self.assertNotEqual(result.returncode, 0, result.stdout + result.stderr)
                    self.assertIn('escapes expected prefix', result.stderr)

    def test_mapped_internal_and_ordered_generic_fallback(self):
        for scenario, mapping in (('mapped-internal', 'CUSTOM'),
                                  ('mapped-only-internal', 'CUSTOM'),
                                  ('mapped-foreign', ';CUSTOM'),
                                  ('mapped-foreign', 'MISSING;')):
            with self.subTest(scenario=scenario, mapping=mapping):
                result = self.configure(scenario, mapping=mapping)
                self.assertRegex(result.stdout, r'fixture-selected=.*/private/lib/libfm.so')
                self.assertEqual(result.returncode, 0, result.stdout + result.stderr)

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
