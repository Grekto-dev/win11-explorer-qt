import json
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path

from PIL import Image

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "tools" / "fidelity"))
import measure


class ImageMeasurementTests(unittest.TestCase):
    def test_exact_region_metrics_are_zero(self):
        reference = Image.new("RGBA", (8, 6), (20, 30, 40, 255))
        candidate = reference.copy()
        result = measure.compare_region(reference, candidate, (1, 1, 5, 4))
        self.assertEqual(result["geometry"], {"x": 1, "y": 1, "width": 5, "height": 4})
        self.assertEqual(result["color"]["mae_rgb"], 0.0)
        self.assertEqual(result["alpha"]["mae"], 0.0)
        self.assertEqual(result["contour"]["changed_pixels"], 0)

    def test_rejects_dimension_mismatch_without_explicit_crop(self):
        with self.assertRaisesRegex(ValueError, "dimensions"):
            measure.compare_region(Image.new("RGB", (8, 6)), Image.new("RGB", (7, 6)), None)

    def test_rejects_crop_outside_image(self):
        with self.assertRaisesRegex(ValueError, "crop"):
            measure.compare_region(Image.new("RGB", (8, 6)), Image.new("RGB", (8, 6)), (7, 0, 2, 2))

    def test_timeline_uses_monotonic_timestamps_without_claiming_frame_rate(self):
        result = measure.measure_timeline([
            {"file": "a.png", "timestamp_ms": 100.0},
            {"file": "b.png", "timestamp_ms": 137.0},
            {"file": "c.png", "timestamp_ms": 184.0},
        ])
        self.assertEqual(result["duration_ms"], 84.0)
        self.assertEqual(result["observed_intervals_ms"], [37.0, 47.0])
        self.assertNotIn("fps", result)
        with self.assertRaisesRegex(ValueError, "strictly increasing"):
            measure.measure_timeline([
                {"file": "a.png", "timestamp_ms": 2},
                {"file": "b.png", "timestamp_ms": 2},
            ])

    def test_pass_requires_explicit_measured_criteria(self):
        self.assertEqual(measure.evaluate_criteria({"mae_rgb": 0.0}, None)["status"], "not_evaluated")
        with self.assertRaisesRegex(ValueError, "marked measured"):
            measure.evaluate_criteria({"mae_rgb": 0.0}, {"status": "measured", "max_mae_rgb": 1.0})
        result = measure.evaluate_criteria(
            {"mae_rgb": 0.0},
            {"status": "measured", "source": "repeatability study", "max_mae_rgb": 0.0},
        )
        self.assertEqual(result["status"], "measured")
        self.assertEqual(result["equivalence"], "pass")


class EvidenceManifestTests(unittest.TestCase):
    def test_validates_version_states_artifacts_and_environment(self):
        manifest = {
            "schema_version": 1,
            "environment": {"build": "test", "theme": "test", "locale": "en-US", "dpi": 96,
                             "scale": 1.0, "resolution": [8, 6], "client_rect": [0, 0, 8, 6],
                             "refresh_hz": 60, "capture_method": "fixture"},
            "cases": [{"id": "idle", "status": "measured", "artifacts": ["frame.png"],
                       "measurements": [], "limitations": []}],
            "frames": [{"file": "frame.png", "timestamp_ms": 1.0, "region": [0, 0, 8, 6]}],
        }
        self.assertEqual(measure.validate_manifest(manifest)["status"], "valid")
        manifest["cases"][0]["status"] = "pass"
        with self.assertRaisesRegex(ValueError, "status"):
            measure.validate_manifest(manifest)

    def test_cli_compares_real_captures_and_writes_auditable_report(self):
        repo = Path("/home/pedro/win11-explorer-qt")
        reference = repo / ".state/reference/explorer-window.png"
        candidate = repo / ".state/proof-x11/explorer-window.png"
        self.assertTrue(reference.is_file())
        self.assertTrue(candidate.is_file())
        with tempfile.TemporaryDirectory() as temp:
            report = Path(temp) / "report.json"
            command = [sys.executable, str(ROOT / "tools/fidelity/measure.py"), "compare",
                       "--reference", str(reference), "--candidate", str(candidate),
                       "--reference-state", "initial-window", "--candidate-state", "initial-window",
                       "--region", "client,0,0,1104,642", "--report", str(report)]
            completed = subprocess.run(command, text=True, capture_output=True)
            self.assertEqual(completed.returncode, 0, completed.stderr)
            data = json.loads(report.read_text())
            self.assertEqual(data["status"], "not_evaluated")
            self.assertEqual(data["equivalence"], "not_evaluated")
            self.assertIn("tool_versions", data)
            self.assertEqual(data["regions"][0]["name"], "client")
            self.assertTrue(data["regions"][0]["color"]["mae_rgb"] > 0)

    def test_cli_rejects_incompatible_state_and_missing_evidence(self):
        with tempfile.TemporaryDirectory() as temp:
            a, b = Path(temp) / "a.png", Path(temp) / "b.png"
            Image.new("RGB", (4, 4)).save(a)
            Image.new("RGB", (4, 4)).save(b)
            script = str(ROOT / "tools/fidelity/measure.py")
            mismatch = subprocess.run([sys.executable, script, "compare", "--reference", str(a),
                                      "--candidate", str(b), "--reference-state", "idle",
                                      "--candidate-state", "hover"], capture_output=True, text=True)
            self.assertNotEqual(mismatch.returncode, 0)
            self.assertIn("state", mismatch.stderr)
            missing = subprocess.run([sys.executable, script, "validate", str(Path(temp) / "missing.json")],
                                     capture_output=True, text=True)
            self.assertNotEqual(missing.returncode, 0)
            self.assertIn("not found", missing.stderr)

    def test_cli_reports_timeline_from_manifest_without_inventing_fps(self):
        with tempfile.TemporaryDirectory() as temp:
            base = Path(temp)
            for name in ("a.png", "b.png"):
                Image.new("RGB", (2, 2)).save(base / name)
            manifest = {"schema_version": 1, "environment": {
                "build": "fixture", "theme": "fixture", "locale": "en-US", "dpi": 96,
                "scale": 1, "resolution": [2, 2], "client_rect": [0, 0, 2, 2],
                "refresh_hz": 60, "capture_method": "synthetic test"},
                "cases": [{"id": "transition", "status": "measured", "artifacts": [],
                           "measurements": [], "limitations": []}],
                "frames": [{"file": "a.png", "timestamp_ms": 50, "region": [0, 0, 2, 2]},
                           {"file": "b.png", "timestamp_ms": 93, "region": [0, 0, 2, 2]}]}
            source, report = base / "manifest.json", base / "timeline.json"
            source.write_text(json.dumps(manifest))
            completed = subprocess.run([sys.executable, str(ROOT / "tools/fidelity/measure.py"),
                                         "timeline", str(source), "--report", str(report)],
                                        text=True, capture_output=True)
            self.assertEqual(completed.returncode, 0, completed.stderr)
            data = json.loads(report.read_text())
            self.assertEqual(data["timelines"][0]["duration_ms"], 43.0)
            self.assertNotIn("fps", data["timelines"][0])


if __name__ == "__main__":
    unittest.main()
