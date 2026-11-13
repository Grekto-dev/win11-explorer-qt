"""Portable synthetic algorithm/contract fixtures; not Windows reference evidence."""
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
        with tempfile.TemporaryDirectory() as temp:
            base = Path(temp)
            for name, color in (("a.png", "black"), ("b.png", "gray"), ("c.png", "white")):
                Image.new("RGB", (2, 2), color).save(base / name)
            result = measure.measure_timeline([
                {"file": "a.png", "timestamp_ms": 100.0},
                {"file": "b.png", "timestamp_ms": 137.0},
                {"file": "c.png", "timestamp_ms": 184.0},
            ], base)
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
        with self.assertRaisesRegex(ValueError, "source evidence"):
            measure.evaluate_criteria({"mae_rgb": 0.0}, {"status": "measured", "max_mae_rgb": 1.0})
        with tempfile.TemporaryDirectory() as temp:
            source = Path(temp) / "repeatability.json"
            source.write_text('{"runs": 3}')
            result = measure.evaluate_criteria(
                {"mae_rgb": 0.0},
                {"status": "measured", "source": {"path": source.name, "sha256": measure._sha256(source)},
                 "max_mae_rgb": 0.0}, base_dir=Path(temp))
        self.assertEqual(result["status"], "measured")
        self.assertEqual(result["equivalence"], "pass")

    def test_criteria_reject_nonfinite_limits_and_unverifiable_sources(self):
        with tempfile.TemporaryDirectory() as temp:
            source = Path(temp) / "criteria.json"
            source.write_text("observed")
            for limit in (float("nan"), float("inf"), float("-inf")):
                with self.subTest(limit=limit), self.assertRaisesRegex(ValueError, "finite"):
                    measure.evaluate_criteria({"mae_rgb": 0.0},
                        {"status": "measured", "source": {"path": source.name, "sha256": measure._sha256(source)},
                         "max_mae_rgb": limit}, base_dir=Path(temp))
        with self.assertRaisesRegex(ValueError, "source evidence"):
            measure.evaluate_criteria({"mae_rgb": 0.0},
                {"status": "measured", "source": "repeatability definitely happened", "max_mae_rgb": 1})
        with tempfile.TemporaryDirectory() as temp:
            evidence = Path(temp) / "claimed.json"
            evidence.write_text("not the claimed measurement")
            with self.assertRaisesRegex(ValueError, "sha256 mismatch"):
                measure.evaluate_criteria({"mae_rgb": 0.0},
                    {"status": "measured", "source": {"path": evidence.name, "sha256": "0" * 64},
                     "max_mae_rgb": 1}, base_dir=Path(temp))

    def test_timeline_curve_measures_pixels_and_bounds_observable_change(self):
        with tempfile.TemporaryDirectory() as temp:
            base = Path(temp)
            Image.new("RGB", (4, 4), "black").save(base / "a.png")
            middle = Image.new("RGB", (4, 4), "black")
            for x in range(2):
                for y in range(4):
                    middle.putpixel((x, y), (255, 255, 255))
            middle.save(base / "b.png")
            Image.new("RGB", (4, 4), "white").save(base / "c.png")
            result = measure.measure_timeline([
                {"file": "a.png", "timestamp_ms": 100, "region": [0, 0, 4, 4]},
                {"file": "b.png", "timestamp_ms": 150, "region": [0, 0, 4, 4]},
                {"file": "c.png", "timestamp_ms": 220, "region": [0, 0, 4, 4]},
            ], base)
            self.assertEqual([point["changed_fraction"] for point in result["curve"]], [0, .5, .5])
            self.assertEqual(result["first_change_interval_ms"], [100.0, 150.0])
            self.assertEqual(result["last_change_interval_ms"], [150.0, 220.0])
            self.assertNotIn("exact_duration_ms", result)

    def test_event_associations_bracket_event_and_next_measured_roi_change(self):
        timeline = {"curve": [
            {"timestamp_ms": 100.0, "frame": 0, "changed_fraction": 0.0},
            {"timestamp_ms": 150.0, "frame": 1, "changed_fraction": 0.0},
            {"timestamp_ms": 220.0, "frame": 2, "changed_fraction": 0.25},
            {"timestamp_ms": 300.0, "frame": 3, "changed_fraction": 0.0}]}
        result = measure.associate_timeline_events(timeline, [
            {"label": "sort-open", "timestamp_ms": 160.0},
            {"label": "menu-close", "timestamp_ms": 250.0}])
        self.assertEqual(result[0]["event_bracket_ms"], [150.0, 220.0])
        self.assertEqual(result[0]["next_observed_change_interval_ms"], [150.0, 220.0])
        self.assertEqual(result[0]["next_observed_change_frame"], 2)
        self.assertIsNone(result[1]["next_observed_change_interval_ms"])



class EvidenceManifestTests(unittest.TestCase):
    def test_screen_space_region_dict_becomes_local_png_roi_without_losing_origin(self):
        with tempfile.TemporaryDirectory() as temp:
            base = Path(temp)
            Image.new("RGB", (640, 400)).save(base / "screen-roi.png")
            manifest = {"schema_version": 1, "environment": {},
                "cases": [{"id": "x", "status": "not_observed", "artifacts": [], "measurements": [], "limitations": []}],
                "frames": [{"file": "screen-roi.png", "timestamp_ms": 1,
                            "region": {"x": 480, "y": 240, "width": 640, "height": 400}}]}
            result = measure.validate_manifest(manifest, base)
            frame = result["normalized_manifest"]["frames"][0]
            self.assertEqual(frame["region"], [0, 0, 640, 400])
            self.assertEqual(frame["screen_origin"], [480, 240])
            self.assertEqual(frame["region_coordinate_space"], "screen")
            self.assertEqual(result["normalized_manifest"]["provenance"]["source_frames"], manifest["frames"])

    def test_local_region_list_remains_local_and_screen_origin_is_not_inferred(self):
        with tempfile.TemporaryDirectory() as temp:
            base = Path(temp)
            Image.new("RGB", (640, 400)).save(base / "local.png")
            manifest = {"schema_version": 1, "environment": {},
                "cases": [{"id": "x", "status": "not_observed", "artifacts": [], "measurements": [], "limitations": []}],
                "frames": [{"file": "local.png", "timestamp_ms": 1, "region": [0, 0, 640, 400]}]}
            frame = measure.validate_manifest(manifest, base)["normalized_manifest"]["frames"][0]
            self.assertEqual(frame["region"], [0, 0, 640, 400])
            self.assertNotIn("screen_origin", frame)
            self.assertEqual(frame["region_coordinate_space"], "image")

    def test_measured_environment_objects_normalize_without_erasing_unknown_refresh(self):
        environment = {"build": "Windows 10 Pro; DisplayVersion 26H2; build 26300; UBR 9711",
            "theme": "light", "language": "pt-BR", "dpi": {"GetDpiForWindow": 96, "scale_percent": 100},
            "resolution_px": [1440, 900], "client_rect_px": {"client_rect": [0, 0, 1123, 592],
                "client_screen_origin": [194, 162], "dwm_extended_frame": [193, 162, 1318, 755]},
            "refresh_rate_hz": None, "capture_method": "measured"}
        manifest = {"schema_version": 1, "environment": environment,
            "cases": [{"id": "x", "status": "not_observed", "artifacts": [], "measurements": [], "limitations": []}]}
        result = measure.validate_manifest(manifest)
        self.assertEqual(result["environment"]["dpi"], 96)
        self.assertEqual(result["environment"]["scale"], 1.0)
        self.assertEqual(result["environment"]["client_rect"], [0, 0, 1123, 592])
        self.assertEqual(result["environment"]["refresh_hz"], "unknown")
        self.assertEqual(result["quality_gaps"], ["refresh_hz"])
        self.assertFalse(result["equality_eligible"])
        self.assertEqual(result["normalized_manifest"]["provenance"]["producer_environment"], environment)

    def test_measured_cases_require_nonempty_measurement_and_supporting_artifacts(self):
        base = {"schema_version": 1, "environment": {}, "cases": [
            {"id": "x", "status": "measured", "artifacts": [], "measurements": {}, "limitations": []}]}
        with self.assertRaisesRegex(ValueError, "measured case.*artifact"):
            measure.validate_manifest(base)
        base["cases"][0]["artifacts"] = ["frame.png"]
        with self.assertRaisesRegex(ValueError, "measured case.*measurement"):
            measure.validate_manifest(base)
        base["cases"][0]["measurements"] = {"reported_value": None}
        with self.assertRaisesRegex(ValueError, "measured case.*measurement"):
            measure.validate_manifest(base)

    def test_frame_regions_require_finite_integer_positive_local_coordinates(self):
        with tempfile.TemporaryDirectory() as temp:
            base = Path(temp)
            Image.new("RGB", (5, 5)).save(base / "frame.png")
            manifest = {"schema_version": 1, "environment": {},
                "cases": [{"id": "x", "status": "not_observed", "artifacts": [], "measurements": [], "limitations": []}],
                "frames": [{"file": "frame.png", "timestamp_ms": 1, "region": [0, 0, 5, 5]}]}
            self.assertEqual(measure.validate_manifest(manifest, base)["status"], "valid")
            for region in ([True, 0, 1, 1], [0.5, 0, 1, 1], [0, 0, float("nan"), 1],
                           [0, 0, float("inf"), 1], [0, 0, "2", 1],
                           [-1, 0, 1, 1], [0, 0, 0, 1], [4, 4, 2, 2]):
                with self.subTest(region=region), self.assertRaises(ValueError):
                    manifest["frames"][0]["region"] = region
                    measure.validate_manifest(manifest, base)

    def test_rejects_absolute_artifacts_outside_authorized_phase2_root(self):
        with tempfile.TemporaryDirectory() as temp:
            base = Path(temp) / ".state/fidelity/reference/phase2/synthetic"
            base.mkdir(parents=True)
            manifest = {"schema_version": 1, "environment": {}, "cases": [
                {"id": "synthetic", "status": "not_observed", "artifacts": [str(Path(temp) / "outside")],
                 "measurements": [], "limitations": ["Synthetic fixture, not Windows evidence"]}]}
            with self.assertRaisesRegex(ValueError, "authorized phase2"):
                measure.validate_manifest(manifest, base)

    def test_rejects_relative_symlink_artifact_escaping_manifest_directory(self):
        with tempfile.TemporaryDirectory() as temp, tempfile.TemporaryDirectory() as outside:
            base, outside_dir = Path(temp), Path(outside)
            Image.new("RGB", (1, 1)).save(outside_dir / "frame.png")
            (base / "frame.png").symlink_to(outside_dir / "frame.png")
            manifest = {"schema_version": 1, "environment": {},
                "cases": [{"id": "x", "status": "not_observed", "artifacts": [], "measurements": {}, "limitations": []}],
                "frames": [{"file": "frame.png", "timestamp_ms": 1, "region": [0, 0, 1, 1]}]}
            with self.assertRaisesRegex(ValueError, "escapes manifest directory"):
                measure.validate_manifest(manifest, base)

    def test_validates_version_states_artifacts_and_environment(self):
        manifest = {
            "schema_version": 1,
            "environment": {"build": "test", "theme": "test", "locale": "en-US", "dpi": 96,
                             "scale": 1.0, "resolution": [8, 6], "client_rect": [0, 0, 8, 6],
                             "refresh_hz": 60, "capture_method": "fixture"},
            "cases": [{"id": "idle", "status": "measured", "artifacts": ["frame.png"],
                       "measurements": {"visual_state": "idle"}, "limitations": []}],
            "frames": [{"file": "frame.png", "timestamp_ms": 1.0, "region": [0, 0, 8, 6]}],
        }
        self.assertEqual(measure.validate_manifest(manifest)["status"], "valid")
        manifest["frames"].append({"file": "frame.png", "timestamp_ms": 1.0, "region": [0, 0, 8, 6]})
        with self.assertRaisesRegex(ValueError, "strictly increasing"):
            measure.validate_manifest(manifest)
        manifest["frames"].pop()
        manifest["cases"][0]["status"] = "pass"
        with self.assertRaisesRegex(ValueError, "status"):
            measure.validate_manifest(manifest)

    def test_cli_compares_synthetic_images_and_writes_auditable_report(self):
        with tempfile.TemporaryDirectory() as temp:
            base = Path(temp)
            reference, candidate = base / "synthetic-reference.png", base / "synthetic-candidate.png"
            Image.new("RGB", (8, 6), "black").save(reference)
            Image.new("RGB", (8, 6), "white").save(candidate)
            report = base / "report.json"
            command = [sys.executable, str(ROOT / "tools/fidelity/measure.py"), "compare",
                       "--reference", str(reference), "--candidate", str(candidate),
                       "--reference-label", "synthetic unit fixture, not Windows evidence",
                       "--candidate-label", "synthetic unit fixture, not Windows evidence",
                       "--reference-state", "synthetic-idle", "--candidate-state", "synthetic-idle",
                       "--region", "client,0,0,8,6", "--report", str(report)]
            completed = subprocess.run(command, text=True, capture_output=True)
            self.assertEqual(completed.returncode, 0, completed.stderr)
            data = json.loads(report.read_text())
            self.assertEqual(data["status"], "not_evaluated")
            self.assertEqual(data["equivalence"], "not_evaluated")
            self.assertIn("tool_versions", data)
            self.assertEqual(data["regions"][0]["name"], "client")
            self.assertEqual(data["regions"][0]["color"]["mae_rgb"], 255)
            self.assertIn("synthetic", data["capture_metadata"]["reference_label"])

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
                "cases": [{"id": "transition", "status": "measured", "artifacts": ["a.png"],
                           "measurements": {"sample_count": 2}, "limitations": []}],
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
            self.assertEqual(data["manifest_validation"]["environment"]["dpi"], 96)


if __name__ == "__main__":
    unittest.main()
