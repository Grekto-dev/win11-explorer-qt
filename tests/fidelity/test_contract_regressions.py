"""Authorized contract regressions: destructive probes use synthetic inputs only."""
import copy
import json
from pathlib import Path
import tempfile
import unittest

from PIL import Image

import test_output_measurement_regressions as output_tests
from test_review_regressions import synthetic_manifest
import measure


class ContractRegressionTests(unittest.TestCase):
    cli = output_tests.OutputMeasurementRegressionTests.cli

    def fixture(self, base):
        Image.new("RGB", (8, 6), "black").save(base / "reference.png")
        Image.new("RGB", (8, 6), "white").save(base / "candidate.png")
        manifest = synthetic_manifest()
        manifest["environment"]["refresh_hz"] = "unknown"
        manifest["frames"] = [{"file": "reference.png", "timestamp_ms": 0,
                               "region": [0, 0, 8, 6]}]
        return manifest

    def test_output_file_ancestry_is_rejected_before_any_write(self):
        for kind in ("report_child", "report_parent", "existing_parent", "input_parent", "input_child"):
            with self.subTest(kind=kind), tempfile.TemporaryDirectory() as temp:
                base = Path(temp)
                self.fixture(base)
                heat, overlay = base / "heat", base / "overlay"
                report = base / "report.json"
                (base / "overlay.full.png").write_bytes(b"previous synthetic overlay")
                if kind == "report_child":
                    report = base / "heat.full.png" / "report.json"
                elif kind == "report_parent":
                    report = base / "container"
                    heat = report / "heat"
                elif kind == "existing_parent":
                    (base / "container").write_bytes(b"existing synthetic file")
                    report = base / "container" / "report.json"
                elif kind == "input_parent":
                    container = base / "input-container"
                    container.mkdir()
                    (base / "reference.png").rename(container / "reference.png")
                    report = container
                else:
                    report = base / "reference.png" / "report.json"
                reference = (base / "input-container/reference.png" if kind == "input_parent"
                             else base / "reference.png")
                before = {str(p.relative_to(base)): p.read_bytes() if p.is_file() else None
                          for p in base.rglob("*")}
                result = self.cli("compare", "--reference", reference,
                    "--candidate", base / "candidate.png", "--reference-state", "synthetic",
                    "--candidate-state", "synthetic", "--heatmap", heat, "--overlay", overlay,
                    "--report", report)
                self.assertEqual(result.returncode, 2, result.stdout + result.stderr)
                self.assertNotIn("Traceback", result.stderr)
                self.assertEqual({str(p.relative_to(base)): p.read_bytes() if p.is_file() else None
                                  for p in base.rglob("*")}, before)

    def test_unknown_events_keep_raw_normalized_timeline_coherent(self):
        for status in ("partial", "not_observed", "blocked", "measured"):
            for payload in (None, "unknown", "not_observed", "not-measured", "", []):
                for container in ("keyed", "named"):
                    with self.subTest(status=status, payload=payload, container=container), tempfile.TemporaryDirectory() as temp:
                        base = Path(temp)
                        manifest = self.fixture(base)
                        values = {"input_events": payload, "duration_ms": 0, "held": False}
                        measurements = (values if container == "keyed" else
                                        [{"name": k, "value": v} for k, v in values.items()])
                        manifest["cases"][0].update(status=status, artifacts=["reference.png"],
                                                   measurements=measurements)
                        first = measure.validate_manifest(manifest, base)
                        self.assertEqual(measure.validate_manifest(first["normalized_manifest"], base), first)
                        source, report = base / "manifest.json", base / "report.json"
                        reports = []
                        for variant in (manifest, first["normalized_manifest"]):
                            source.write_text(json.dumps(variant))
                            original = source.read_bytes()
                            validation = self.cli("validate", source)
                            self.assertEqual(validation.returncode, 0, validation.stderr)
                            result = self.cli("timeline", source, "--report", report)
                            self.assertEqual(result.returncode, 0, result.stderr)
                            self.assertEqual(source.read_bytes(), original)
                            data = json.loads(report.read_text())
                            data.pop("generated_utc")
                            self.assertEqual(data["event_associations"], {})
                            self.assertEqual(data["status"], "measured" if status == "measured" else "not_evaluated")
                            self.assertEqual(data["equivalence"], "not_evaluated")
                            self.assertFalse(data["manifest_validation"]["equality_eligible"])
                            self.assertEqual(data["manifest_validation"]["quality_gaps"], ["refresh_hz"])
                            reports.append(data)
                        self.assertEqual(reports[0], reports[1])

    def test_shared_extras_do_not_select_legacy_or_bypass_validation(self):
        with tempfile.TemporaryDirectory() as temp:
            base = Path(temp)
            original = self.fixture(base)
            original["cases"][0]["measurements"] = {"input_events": [{"timestamp_ms": 0, "label": "synthetic"}]}
            source, report = base / "manifest.json", base / "report.json"
            reports = []
            for extras in ({}, {"start_utc": "synthetic metadata"},
                           {"start_utc": "synthetic metadata", "input": "extra"}):
                manifest = copy.deepcopy(original)
                manifest.update(extras)
                if "input" in extras:
                    manifest["frames"][0]["elapsed_ms"] = 0
                normalized = measure.validate_manifest(manifest, base)["normalized_manifest"]
                for variant in (manifest, normalized):
                    source.write_text(json.dumps(variant))
                    self.assertEqual(self.cli("validate", source).returncode, 0)
                    result = self.cli("timeline", source, "--report", report)
                    self.assertEqual(result.returncode, 0, result.stderr)
                    data = json.loads(report.read_text())
                    data.pop("generated_utc")
                    self.assertIsNotNone(data["manifest_validation"])
                    self.assertEqual(data["manifest_validation"]["quality_gaps"], ["refresh_hz"])
                    self.assertFalse(data["manifest_validation"]["equality_eligible"])
                    self.assertTrue(data["event_associations"])
                    reports.append(data)
            self.assertEqual(reports, [reports[0]] * len(reports))

    def test_ambiguous_shared_cannot_escape_validation_via_legacy(self):
        for defect in ("environment", "cases", "schema_version"):
            with self.subTest(defect=defect), tempfile.TemporaryDirectory() as temp:
                base = Path(temp) / ".state/fidelity/reference/phase2/synthetic"
                base.mkdir(parents=True)
                manifest = self.fixture(base)
                manifest.update(start_utc="synthetic")
                manifest["frames"][0]["elapsed_ms"] = 0
                manifest[defect] = None
                source, report = base / "manifest.json", Path(temp) / "report.json"
                source.write_text(json.dumps(manifest))
                report.write_bytes(b"previous synthetic report")
                self.assertEqual(self.cli("validate", source).returncode, 2)
                result = self.cli("timeline", source, "--report", report)
                self.assertEqual(result.returncode, 2, result.stdout + result.stderr)
                self.assertNotIn("Traceback", result.stderr)
                self.assertEqual(report.read_bytes(), b"previous synthetic report")

    def test_event_payload_matrix_rejects_invalid_without_hiding_neighbors(self):
        valid = [{"timestamp_ms": 0, "label": "synthetic"}]
        invalid = ({}, False, 0, "observed", [None], [{}], [{"timestamp_ms": False}],
                   [{"timestamp_ms": float("nan")}], [{"timestamp_ms": float("inf")}],
                   [*valid, {}])
        with tempfile.TemporaryDirectory() as temp:
            base = Path(temp)
            original = self.fixture(base)
            source, report = base / "manifest.json", base / "report.json"
            for payload in invalid:
                for container in ("keyed", "named"):
                    with self.subTest(payload=payload, container=container):
                        manifest = copy.deepcopy(original)
                        values = {"input_events": payload, "duration_ms": 0}
                        manifest["cases"][0]["measurements"] = (values if container == "keyed" else
                            [{"name": "input_events", "value": None},
                             *[{"name": k, "value": v} for k, v in values.items()]])
                        source.write_text(json.dumps(manifest))
                        report.write_bytes(b"previous report")
                        result = self.cli("timeline", source, "--report", report)
                        self.assertEqual(result.returncode, 2, result.stdout + result.stderr)
                        self.assertNotIn("Traceback", result.stderr)
                        self.assertEqual(report.read_bytes(), b"previous report")
            # No event measurement and named record without value are both absent.
            # An explicit unknown does not become observed merely by naming it.
            for values in ({}, {"input_events": None}, [{"name": "input_events"}],
                           [{"name": "input_events", "value": None}]):
                for status in ("partial", "not_observed", "measured"):
                    with self.subTest(values=values, status=status):
                        manifest = copy.deepcopy(original)
                        manifest["cases"][0].update(status=status, artifacts=["reference.png"], measurements=values)
                        source.write_text(json.dumps(manifest))
                        result = self.cli("timeline", source, "--report", report)
                        self.assertEqual(result.returncode, 2 if status == "measured" else 0, result.stderr)
                        if status != "measured":
                            self.assertEqual(json.loads(report.read_text())["event_associations"], {})

    def test_legacy_schema_metadata_does_not_declare_shared_family(self):
        with tempfile.TemporaryDirectory() as temp:
            base = Path(temp) / ".state/fidelity/reference/phase2/synthetic"
            base.mkdir(parents=True)
            self.fixture(base)
            source, report = base / "legacy.json", Path(temp) / "report.json"
            reports = []
            for metadata in ({}, {"schema_version": 1}, {"schema_version": "producer-local"},
                             {"schema_version": 7, "description": "extra metadata"}):
                manifest = {"start_utc": "synthetic", "input": "synthetic",
                            "frames": [{"file": "reference.png", "elapsed_ms": 0}], **metadata}
                source.write_text(json.dumps(manifest))
                before = source.read_bytes()
                result = self.cli("timeline", source, "--report", report)
                self.assertEqual(result.returncode, 0, result.stderr)
                self.assertEqual(source.read_bytes(), before)
                data = json.loads(report.read_text())
                data.pop("generated_utc")
                self.assertIsNone(data["manifest_validation"])
                self.assertEqual(data["timestamp_provenance"], "transfer-ui-timeline.json elapsed_ms")
                self.assertEqual(data["equivalence"], "not_evaluated")
                reports.append(data)
            self.assertEqual(reports, [reports[0]] * len(reports))


if __name__ == "__main__":
    unittest.main()
