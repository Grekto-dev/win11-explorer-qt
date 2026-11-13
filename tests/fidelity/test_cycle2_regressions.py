"""Cycle-2 regressions: synthetic sandbox only, never Windows evidence."""
import copy
import hashlib
import json
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path

from PIL import Image

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "tools/fidelity"))
import measure
from test_review_regressions import synthetic_manifest


class Cycle2Tests(unittest.TestCase):
    def cli(self, source, report):
        return subprocess.run([sys.executable, str(ROOT / "tools/fidelity/measure.py"),
                               "timeline", str(source), "--report", str(report)],
                              text=True, capture_output=True)

    def fixture(self, base):
        Image.new("RGB", (8, 6), "black").save(base / "sentinel.png")
        manifest = synthetic_manifest()
        manifest["frames"] = [{"file": "sentinel.png", "timestamp_ms": 100,
                               "region": [0, 0, 8, 6]}]
        source = base / "manifest.json"
        source.write_text(json.dumps(manifest))
        return manifest, source

    def test_s1_cli_rejects_symlink_components_and_final_without_touching_sentinel(self):
        for kind in ("final", "ancestor", "hardlink", "source"):
            with self.subTest(kind=kind), tempfile.TemporaryDirectory() as temp:
                base = Path(temp)
                _, source = self.fixture(base)
                sentinel = base / "sentinel.png"
                before = hashlib.sha256(sentinel.read_bytes()).hexdigest()
                reports = base / "reports"
                reports.mkdir()
                report = reports / "real-152-timeline.json"
                if kind == "final":
                    report.symlink_to(sentinel)
                elif kind == "ancestor":
                    (reports / "linked").symlink_to(base, target_is_directory=True)
                    report = reports / "linked" / "derived.json"
                elif kind == "hardlink":
                    report.hardlink_to(sentinel)
                else:
                    report = sentinel
                completed = self.cli(source, report)
                after = hashlib.sha256(sentinel.read_bytes()).hexdigest()
                self.assertEqual(after, before, "synthetic sentinel was modified")
                self.assertEqual(completed.returncode, 2, completed.stderr)
                self.assertNotIn("Traceback", completed.stderr)
                self.assertFalse((base / "derived.json").exists())


if __name__ == "__main__":
    unittest.main()
