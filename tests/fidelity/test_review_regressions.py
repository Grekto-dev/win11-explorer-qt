"""Synthetic contract fixtures only; not Windows captures or parity evidence."""
import copy
import sys
import tempfile
import unittest
from unittest import mock
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[2] / "tools/fidelity"))
import measure


def synthetic_manifest():
    return {"schema_version": 1, "environment": {
        "build": "synthetic", "theme": "synthetic", "locale": "pt-BR", "dpi": 96,
        "scale": 1, "resolution": [8, 6], "client_rect": [0, 0, 8, 6],
        "refresh_hz": 60, "capture_method": "synthetic unit fixture"},
        "cases": [{"id": "synthetic", "status": "not_observed", "artifacts": [],
                   "measurements": [], "limitations": ["Synthetic fixture; not Windows evidence"]}]}


class EnvironmentRegressionTests(unittest.TestCase):
    def test_canonical_environment_survives_without_aliases(self):
        manifest = synthetic_manifest()
        original = copy.deepcopy(manifest)
        result = measure.validate_manifest(manifest)
        self.assertEqual(result["environment"], manifest["environment"])
        self.assertEqual(result["quality_gaps"], [])
        self.assertTrue(result["equality_eligible"])
        self.assertEqual(manifest, original)

    def test_alias_fallback_canonical_precedence_and_explicit_refresh_null(self):
        aliases = {"locale": "language", "resolution": "resolution_px", "client_rect": "client_rect_px",
                   "refresh_hz": "refresh_rate_hz"}
        for mode in ("aliases", "both"):
            manifest = synthetic_manifest()
            for canonical, alias in aliases.items():
                manifest["environment"][alias] = copy.deepcopy(manifest["environment"][canonical])
                if mode == "aliases":
                    del manifest["environment"][canonical]
                else:
                    manifest["environment"][alias] = None
            with self.subTest(mode=mode):
                result = measure.validate_manifest(manifest)
                for key in aliases:
                    self.assertEqual(result["environment"][key], synthetic_manifest()["environment"][key])
                self.assertEqual(result["quality_gaps"], [])
        manifest = synthetic_manifest()
        del manifest["environment"]["refresh_hz"]
        manifest["environment"].update(refresh_rate_hz=None, refresh_rate_hz_reported=60)
        result = measure.validate_manifest(manifest)
        self.assertEqual(result["environment"]["refresh_hz"], "unknown")
        self.assertEqual(result["quality_gaps"], ["refresh_hz"])

    def test_normalize_twice_preserves_original_provenance_and_screen_roi(self):
        manifest = synthetic_manifest()
        manifest["environment"].pop("refresh_hz")
        manifest["environment"]["refresh_rate_hz_reported"] = 1
        manifest["frames"] = [{"file": "synthetic.png", "monotonic_ns": 123000000,
                               "region": {"x": 480, "y": 240, "width": 8, "height": 6}}]
        first = measure.validate_manifest(manifest)
        second = measure.validate_manifest(first["normalized_manifest"])
        self.assertEqual(second, first)
        self.assertEqual(second["normalized_manifest"]["provenance"]["producer_environment"], manifest["environment"])
        self.assertEqual(second["normalized_manifest"]["provenance"]["source_frames"], manifest["frames"])


class MeasurementRegressionTests(unittest.TestCase):
    def test_names_units_and_descriptions_are_not_observed_values(self):
        with tempfile.TemporaryDirectory() as temp:
            base = Path(temp)
            (base / "support.txt").write_text("synthetic support only")
            for measurements in ([{"name": "duration_ms", "value": None, "unit": "ms", "description": "duration"}],
                                 [{"name": "duration_ms"}],
                                 {"duration_ms": None, "description": "duration", "unit": "ms"},
                                 [{"name": "state", "value": "not_observed"}]):
                manifest = synthetic_manifest()
                manifest["cases"][0].update(status="measured", artifacts=["support.txt"], measurements=measurements)
                with self.subTest(measurements=measurements), self.assertRaisesRegex(ValueError, "measurement"):
                    measure.validate_manifest(manifest, base)

    def test_nonfinite_anywhere_cannot_be_masked_by_adjacent_observation(self):
        for invalid in (float("nan"), float("inf"), float("-inf")):
            for measurements in ([{"name": "duration_ms", "value": invalid}],
                                 [{"name": "count", "value": 0}, {"name": "duration_ms", "value": invalid}],
                                 {"count": False, "nested": {"duration_ms": invalid}},
                                 {"values": [0, {"duration_ms": invalid}]}):
                for status in ("measured", "not_observed", "blocked"):
                    manifest = synthetic_manifest()
                    manifest["cases"][0].update(status=status, artifacts=["synthetic.txt"], measurements=measurements)
                    with self.subTest(invalid=invalid, measurements=measurements, status=status):
                        with self.assertRaisesRegex(ValueError, "finite"):
                            measure.validate_manifest(manifest)

    def test_zero_false_and_categorical_values_survive_normalization_twice(self):
        for value in (0, False, "idle", {"observed": False}, [0, None]):
            for measurements in ({"observed_value": value}, [{"name": "observed_value", "value": value, "unit": "synthetic"}]):
                manifest = synthetic_manifest()
                manifest["cases"][0].update(status="measured", artifacts=["synthetic.txt"], measurements=measurements)
                with self.subTest(value=value, measurements=measurements):
                    first = measure.validate_manifest(manifest)
                    second = measure.validate_manifest(first["normalized_manifest"])
                    self.assertEqual(second, first)
                    self.assertEqual(first["normalized_manifest"]["cases"][0]["measurements"][0]["value"], value)

    def test_explicit_unknown_not_observed_stays_unknown(self):
        manifest = synthetic_manifest()
        measurements = {"duration_ms": None, "state": "not_observed", "cadence": "unknown"}
        manifest["cases"][0]["measurements"] = measurements
        result = measure.validate_manifest(manifest)
        self.assertEqual(result["normalized_manifest"]["cases"][0]["producer_measurements"], measurements)
        self.assertEqual(result["normalized_manifest"]["cases"][0]["status"], "not_observed")


class ArtifactRegressionTests(unittest.TestCase):
    def test_top_level_artifact_types_are_validated_without_base_directory(self):
        for artifacts in (None, "a.zip", {}, [None], [0], [""], [["a.zip"]]):
            manifest = synthetic_manifest()
            manifest["artifacts"] = artifacts
            with self.subTest(artifacts=artifacts), self.assertRaisesRegex(ValueError, "artifact"):
                measure.validate_manifest(manifest)

    def test_top_level_artifacts_require_existing_regular_files(self):
        with tempfile.TemporaryDirectory() as temp:
            base = Path(temp)
            manifest = synthetic_manifest()
            (base / "synthetic.zip").write_bytes(b"synthetic artifact, not a Windows capture")
            manifest["artifacts"] = ["synthetic.zip"]
            self.assertEqual(measure.validate_manifest(manifest, base)["status"], "valid")
            for name in ("missing.zip", "."):
                manifest["artifacts"] = [name]
                with self.subTest(name=name), self.assertRaisesRegex(ValueError, "not found"):
                    measure.validate_manifest(manifest, base)

    def test_top_level_relative_and_symlink_escape_are_rejected_before_external_stat_or_read(self):
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            base = root / "authorized" / "run"
            base.mkdir(parents=True)
            outside = root / "outside"
            outside.write_text("synthetic outside sentinel")
            (base / "escape.zip").symlink_to(outside)
            original_is_file = Path.is_file
            def guarded_is_file(path):
                self.assertTrue(path.is_relative_to(base), "stat outside authorized root")
                return original_is_file(path)
            for path in ("../../outside", "escape.zip"):
                manifest = synthetic_manifest()
                manifest["artifacts"] = [path]
                with self.subTest(path=path), mock.patch.object(Path, "is_file", guarded_is_file):
                    with mock.patch.object(Path, "open", side_effect=AssertionError("unexpected content read")):
                        with self.assertRaisesRegex(ValueError, "escapes manifest directory"):
                            measure.validate_manifest(manifest, base)

    def test_absolute_phase2_top_level_escape_uses_same_containment_guard(self):
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            phase2 = root / ".state/fidelity/reference/phase2"
            base = phase2 / "synthetic-run"
            base.mkdir(parents=True)
            inside = phase2 / "synthetic.txt"
            inside.write_text("synthetic only")
            outside = root / "outside"
            outside.write_text("synthetic outside")
            (base / "link").symlink_to(outside)
            manifest = synthetic_manifest()
            manifest["artifacts"] = [str(inside)]
            self.assertEqual(measure.validate_manifest(manifest, base)["status"], "valid")
            for path in (outside, base / "link"):
                manifest["artifacts"] = [str(path)]
                with self.subTest(path=path), self.assertRaisesRegex(ValueError, "authorized phase2"):
                    measure.validate_manifest(manifest, base)


if __name__ == "__main__":
    unittest.main()
