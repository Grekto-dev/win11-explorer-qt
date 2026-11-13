#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Real ELF loader traces; negative cases never execute the consumer main."""
import os
from pathlib import Path
import shutil
import subprocess
import unittest

from private_libfm_fixture import fixture_plan

ROOT = Path(__file__).resolve().parents[1]


class PrivateRuntime(unittest.TestCase):
    def test_soname_identity_before_launch(self):
        overlay = Path(os.environ.get('L4_TEST_PREFIX', ROOT / '.state/libfm-overlay-prefix'))
        with fixture_plan(ROOT, overlay, ROOT / '.state/runtime-safety-logs') as (root, logs):
            (root / 'scripts/fidelity').mkdir(parents=True)
            shutil.copy2(ROOT / 'scripts/fidelity/check-private-libfm-runtime.py',
                         root / 'scripts/fidelity/check-private-libfm-runtime.py')
            shutil.copy2(ROOT / 'scripts/run.sh', root / 'scripts/run.sh')
            prefix = root / '.state/private'
            lib = prefix / 'lib'
            lib.mkdir(parents=True)
            foreign = root / 'foreign'
            foreign.mkdir()
            code = root / 'library.c'
            code.write_text('int fixture_value(void) { return 7; }\n')
            artifact = lib / 'libfm-qt6.so.17.0.0'
            for target in (artifact, foreign / artifact.name):
                subprocess.run(['cc', '-shared', '-fPIC', '-Wl,-soname,libfm-qt6.so.17',
                                str(code), '-o', str(target)], check=True, capture_output=True)
            before = (foreign / artifact.name).read_bytes()
            soname = lib / 'libfm-qt6.so.17'
            soname.symlink_to(artifact.name)
            marker = root / 'main-was-executed'
            main = root / 'main.c'
            main.write_text('#include <stdio.h>\nextern int fixture_value(void);\n'
                            'int main(void) { FILE *f=fopen("' + str(marker) + '","w"); '
                            'if (!f) return 2; fclose(f); return fixture_value()!=7; }\n')
            binary = root / '.state/install/bin/win11-explorer-qt'
            binary.parent.mkdir(parents=True)
            manifest = root / '.state/install/share/win11-explorer-qt/private-libfm-runtime.txt'
            manifest.parent.mkdir(parents=True)
            manifest.write_text(str(artifact) + '\n')
            subprocess.run(['cc', str(main), str(artifact), '-o', str(binary)],
                           check=True, capture_output=True)
            env = dict(os.environ, LIBFMQT_OVERLAY_PREFIX=str(prefix))
            for scenario in ('external', 'different-internal', 'correct'):
                soname.unlink()
                if scenario == 'external':
                    soname.symlink_to(foreign / artifact.name)
                elif scenario == 'different-internal':
                    other = lib / 'other.so'
                    shutil.copy2(artifact, other)
                    soname.symlink_to(other.name)
                else:
                    soname.symlink_to(artifact.name)
                trace = subprocess.run(['ldd', str(binary)], env=dict(
                    env, LD_LIBRARY_PATH=str(lib)), text=True, capture_output=True)
                (logs / (scenario + '-ldd.log')).write_text(trace.stdout + trace.stderr)
                self.assertEqual(trace.returncode, 0, trace.stderr)
                self.assertIn(str(soname), trace.stdout)
                result = subprocess.run(['bash', str(root / 'scripts/run.sh')], env=env,
                                        text=True, capture_output=True)
                (logs / (scenario + '-launch.log')).write_text(result.stdout + result.stderr)
                if scenario == 'correct':
                    self.assertEqual(result.returncode, 0, result.stderr)
                    self.assertTrue(marker.exists())
                else:
                    self.assertNotEqual(result.returncode, 0, result.stderr)
                    self.assertIn('Runtime ABI mismatch', result.stderr)
                    self.assertFalse(marker.exists(), 'wrong-ABI main was executed')
                self.assertEqual((foreign / artifact.name).read_bytes(), before)


if __name__ == '__main__':
    unittest.main()
