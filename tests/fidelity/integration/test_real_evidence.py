"""Opt-in read-only integration against private real evidence (never bundled)."""
import json
import os
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[3]
sys.path.insert(0, str(ROOT / "tools/fidelity"))
import measure

EVIDENCE_ROOT = os.environ.get("FIDELITY_REAL_EVIDENCE_ROOT", "")


def _source_snapshot(manifest, published, expected_frames=152):
    """Read-only inventory: count physical PNG paths, not distinct contents."""
    frames = manifest["frames"]
    paths = [measure._resolve_artifact(frame["file"], published.parent) for frame in frames]
    if len(frames) != expected_frames or len(set(paths)) != expected_frames:
        raise ValueError("frame count must match unique resolved PNG paths")
    for path in paths:
        with measure.Image.open(path) as image:
            if image.format != "PNG":
                raise ValueError("frame evidence must be PNG format")
            image.verify()
    artifacts = list(manifest.get("artifacts", []))
    for case in manifest["cases"]:
        artifacts.extend(case["artifacts"])
    paths.extend(measure._resolve_artifact(value, published.parent) for value in artifacts)
    paths.append(published.resolve())
    return {str(path): measure._sha256(path) for path in sorted(set(paths))}


@unittest.skipUnless(EVIDENCE_ROOT, "Set FIDELITY_REAL_EVIDENCE_ROOT to the real phase2 evidence directory")
class RealEvidenceIntegrationTests(unittest.TestCase):
    def test_published_v1_manifest_normalizes_losslessly_and_marks_unknown_quality_gaps(self):
        published = Path(EVIDENCE_ROOT) / "20261031T0003-0300/manifest.json"
        self.assertTrue(published.is_file())
        original = json.loads(published.read_text())
        result = measure.validate_manifest(original, published.parent)
        self.assertEqual(result["status"], "valid")
        self.assertFalse(result["equality_eligible"])
        self.assertIn("dpi", result["quality_gaps"])
        self.assertIn("client_rect", result["quality_gaps"])
        self.assertEqual(result["environment"]["resolution"], [1440, 900])
        self.assertEqual(result["environment"]["scale"], "unknown")
        self.assertEqual(original["environment"]["dpi"], "unknown; registry query returned null")
        self.assertIn("language", result["normalized_manifest"]["environment"])
        self.assertEqual(result["normalized_manifest"]["provenance"]["producer_environment"], original["environment"])
        self.assertEqual(result["case_count"], len(original["cases"]))


    def test_published_interaction_manifest_adapts_monotonic_ns_and_region_px(self):
        published = Path(EVIDENCE_ROOT) / "20261031T0003-0300/INTERACTIONS_READY.json"
        original = json.loads(published.read_text())
        result = measure.validate_manifest(original, published.parent)
        frames = result["normalized_manifest"]["frames"]
        self.assertEqual(len(frames), 6)
        self.assertEqual(frames[0]["timestamp_ms"], original["frames"][0]["monotonic_ns"] / 1_000_000)
        self.assertEqual(frames[0]["region"], original["frames"][0]["region_px"])
        self.assertEqual(result["normalized_manifest"]["provenance"]["source_frames"], original["frames"])


    def test_transfer_timeline_adapter_preserves_elapsed_timestamps_and_image_regions(self):
        reference = Path(EVIDENCE_ROOT) / "20261031T0003-0300"
        source = reference / "transfer-ui-timeline.json"
        adapted = measure.adapt_transfer_timeline(json.loads(source.read_text()), reference)
        self.assertEqual([f["timestamp_ms"] for f in adapted["frames"]], [532.5, 1467.2, 2984.1, 6259.3, 13526.0])
        self.assertTrue(all((reference / f["file"]).is_file() for f in adapted["frames"]))
        self.assertEqual(adapted["timestamp_provenance"], "transfer-ui-timeline.json elapsed_ms")


    def test_real_152_pngs_96_dpi_adapter_and_cli_report(self):
        published = Path(EVIDENCE_ROOT) / "20261031T0022-0300/manifest.json"
        original_bytes = published.read_bytes()
        original = json.loads(original_bytes)
        source_before = _source_snapshot(original, published)
        self.addCleanup(lambda: self.assertEqual(_source_snapshot(original, published), source_before,
                                                "Original evidence changed"))
        result = measure.validate_manifest(original, published.parent)
        self.assertEqual(result["frame_count"], 152)
        self.assertEqual(result["environment"]["dpi"], 96)
        self.assertEqual(result["environment"]["scale"], 1)
        self.assertEqual(result["quality_gaps"], ["refresh_hz"])
        self.assertFalse(result["equality_eligible"])
        self.assertEqual(measure.validate_manifest(result["normalized_manifest"], published.parent), result)
        self.assertEqual(result["normalized_manifest"]["provenance"]["source_frames"], original["frames"])
        self.assertEqual(result["normalized_manifest"]["provenance"]["producer_environment"], original["environment"])
        for source, normalized in zip(original["frames"], result["normalized_manifest"]["frames"]):
            self.assertEqual(normalized["timestamp_ms"], source["timestamp_ms"])
            self.assertEqual(normalized["region"], [0, 0, source["region"]["width"], source["region"]["height"]])
            self.assertEqual(normalized["screen_origin"], [source["region"]["x"], source["region"]["y"]])
        with tempfile.TemporaryDirectory() as temp:
            reports = Path(os.environ.get("FIDELITY_EVIDENCE_REPORT_DIR", temp))
            self.assertFalse(reports.resolve().is_relative_to(Path(EVIDENCE_ROOT).resolve()), "Reports must not modify original evidence")
            report = reports / "real-152-timeline.json"
            validation_report = reports / "real-152-validation.json"
            protected = [published, *[measure._resolve_artifact(f["file"], published.parent)
                                      for f in original["frames"]]]
            measure._check_output_paths([report, validation_report], protected, [EVIDENCE_ROOT])
            completed = subprocess.run([sys.executable, str(ROOT / "tools/fidelity/measure.py"),
                                        "timeline", str(published), "--report", str(report)],
                                       text=True, capture_output=True)
            self.assertEqual(completed.returncode, 0, completed.stderr)
            data = json.loads(report.read_text())
            self.assertEqual(data["timelines"][0]["frame_count"], 152)
            self.assertEqual(data["manifest_validation"]["quality_gaps"], ["refresh_hz"])
            self.assertEqual(data["equivalence"], "not_evaluated")
            summary = {key: result[key] for key in ("status", "frame_count", "quality_gaps", "environment", "equality_eligible")}
            summary.update(equivalence="not_evaluated", environment_parity="not_evaluated",
                           reason="Refresh rate unknown; no measured comparison criteria or candidate supplied")
            measure._write_output(validation_report, (json.dumps(summary, indent=2) + "\n").encode(),
                                  protected, [EVIDENCE_ROOT])
        self.assertEqual(published.read_bytes(), original_bytes)
        self.assertEqual(_source_snapshot(original, published), source_before)


if __name__ == "__main__":
    unittest.main()
