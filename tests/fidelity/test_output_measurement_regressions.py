"""Bounded review regressions; all negative CLI inputs are synthetic."""
import copy
import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest

from PIL import Image

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "tools/fidelity"))
import measure
from test_review_regressions import synthetic_manifest


class OutputMeasurementRegressionTests(unittest.TestCase):
    def cli(self, *args):
        command = [sys.executable, str(ROOT / "tools/fidelity/measure.py"), *map(str, args)]
        result = subprocess.run(command, capture_output=True, text=True)
        log = os.environ.get("FIDELITY_SYNTHETIC_CLI_LOG")
        if log:
            with Path(log).open("a") as stream:
                stream.write(json.dumps({"command": command, "returncode": result.returncode,
                                         "stdout": result.stdout, "stderr": result.stderr}) + "\n")
        return result

    def test_compare_rejects_colliding_output_plan_before_any_write(self):
        for kind in ("heatmap_overlay", "png_report", "duplicate_regions", "lexical_alias",
                     "hardlink", "symlink_ancestor"):
            with self.subTest(kind=kind), tempfile.TemporaryDirectory() as temp:
                base = Path(temp)
                Image.new("RGB", (8, 6), "black").save(base / "reference.png")
                Image.new("RGB", (8, 6), "white").save(base / "candidate.png")
                report = base / "report.json"
                heatmap, overlay = base / "heat", base / "over"
                regions = []
                if kind == "heatmap_overlay":
                    overlay = heatmap
                elif kind == "png_report":
                    report = base / "heat.full.png"
                elif kind == "duplicate_regions":
                    regions = ["--region", "same,0,0,4,4", "--region", "same,1,1,4,4"]
                elif kind == "lexical_alias":
                    overlay = str(base) + "/./heat"
                for name in ("report.json", "heat.full.png", "over.full.png", "heat.same.png", "over.same.png"):
                    (base / name).write_bytes(b"previous synthetic artifact: " + name.encode())
                if kind == "hardlink":
                    (base / "over.full.png").unlink()
                    (base / "over.full.png").hardlink_to(base / "heat.full.png")
                elif kind == "symlink_ancestor":
                    (base / "alias").symlink_to(base, target_is_directory=True)
                    overlay = base / "alias" / "heat"
                before = {p.name: p.read_bytes() for p in base.iterdir() if not p.is_symlink()}
                result = self.cli("compare", "--reference", base / "reference.png",
                    "--candidate", base / "candidate.png", "--reference-state", "synthetic",
                    "--candidate-state", "synthetic", "--heatmap", heatmap, "--overlay", overlay,
                    "--report", report, *regions)
                self.assertEqual(result.returncode, 2, result.stdout + result.stderr)
                self.assertIn("output", result.stderr)
                self.assertNotIn("Traceback", result.stderr)
                self.assertEqual({p.name: p.read_bytes() for p in base.iterdir() if not p.is_symlink()}, before)

    def test_named_records_require_observed_value_not_auxiliary_fields(self):
        with tempfile.TemporaryDirectory() as temp:
            base = Path(temp)
            Image.new("RGB", (8, 6), "black").save(base / "synthetic.png")
            source, report = base / "manifest.json", base / "timeline.json"
            for record in ({"name": "duration_ms", "sample_count": 3},
                           {"name": "duration_ms", "duration_ms": 0, "held": False},
                           {"name": "duration_ms", "value": None, "sample_count": 3}):
                with self.subTest(record=record):
                    manifest = synthetic_manifest()
                    manifest["environment"]["refresh_hz"] = "unknown"
                    manifest["cases"][0].update(status="measured", artifacts=["synthetic.png"],
                                               measurements=[record])
                    manifest["frames"] = [{"file": "synthetic.png", "timestamp_ms": 0,
                                           "region": [0, 0, 8, 6]}]
                    source.write_text(json.dumps(manifest))
                    report.write_text("previous synthetic report")
                    before = {p.name: p.read_bytes() for p in base.iterdir()}
                    for args in (("validate", source), ("timeline", source, "--report", report)):
                        result = self.cli(*args)
                        self.assertEqual(result.returncode, 2, result.stdout + result.stderr)
                        self.assertIn("measurement", result.stderr)
                        self.assertNotIn("Traceback", result.stderr)
                        self.assertEqual({p.name: p.read_bytes() for p in base.iterdir()}, before)
                    with self.assertRaisesRegex(ValueError, "measurement"):
                        measure.validate_manifest(manifest, base)

    def test_keyed_name_and_null_value_preserve_zero_false_metrics_and_idempotence(self):
        for metrics in ({"duration_ms": 0}, {"held": False}, {"duration_ms": 0, "held": False}):
            with self.subTest(metrics=metrics), tempfile.TemporaryDirectory() as temp:
                base = Path(temp)
                Image.new("RGB", (8, 6), "black").save(base / "synthetic.png")
                source, report = base / "manifest.json", base / "timeline.json"
                manifest = synthetic_manifest()
                manifest["environment"]["refresh_hz"] = "unknown"
                values = {"name": "cronometro", "value": None, **metrics}
                manifest["cases"][0].update(status="measured", artifacts=["synthetic.png"], measurements=values)
                manifest["frames"] = [{"file": "synthetic.png", "timestamp_ms": 0, "region": [0, 0, 8, 6]}]
                source.write_text(json.dumps(manifest))
                result = self.cli("validate", source)
                self.assertEqual(result.returncode, 0, result.stderr)
                summary = json.loads(result.stdout)
                self.assertFalse(summary["equality_eligible"])
                self.assertEqual(summary["quality_gaps"], ["refresh_hz"])
                original = copy.deepcopy(manifest)
                first = measure.validate_manifest(manifest, base)
                normalized = first["normalized_manifest"]
                self.assertEqual(manifest, original)
                self.assertEqual(normalized["cases"][0]["producer_measurements"], values)
                self.assertEqual(normalized["cases"][0]["measurements"],
                                 [{"name": key, "value": value} for key, value in values.items()])
                self.assertEqual(measure.validate_manifest(normalized, base), first)
                named = copy.deepcopy(manifest)
                named["cases"][0]["measurements"] = [{"name": key, "value": value} for key, value in metrics.items()]
                reports = []
                for variant in (manifest, normalized, named):
                    source.write_text(json.dumps(variant))
                    before = source.read_bytes()
                    result = self.cli("timeline", source, "--report", report)
                    self.assertEqual(result.returncode, 0, result.stderr)
                    self.assertEqual(source.read_bytes(), before)
                    data = json.loads(report.read_text())
                    data.pop("generated_utc")
                    self.assertEqual(data["status"], "measured")
                    self.assertEqual(data["equivalence"], "not_evaluated")
                    self.assertFalse(data["manifest_validation"]["equality_eligible"])
                    self.assertEqual(data["manifest_validation"]["quality_gaps"], ["refresh_hz"])
                    reports.append(data)
                self.assertEqual(reports, [reports[0]] * 3)


if __name__ == "__main__":
    unittest.main()
