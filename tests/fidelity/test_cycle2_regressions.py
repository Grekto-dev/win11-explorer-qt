"""Cycle-2 regressions: synthetic sandbox only, never Windows evidence."""
import copy
import hashlib
import json
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path
from unittest import mock

sys.path.insert(0, str(Path(__file__).resolve().parent / "integration"))
import test_real_evidence as integration

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

    def test_s1_compare_preserves_criteria_source_and_derived_destinations(self):
        for kind in ("criteria_source", "report", "heatmap", "overlay", "ancestor"):
            with self.subTest(kind=kind), tempfile.TemporaryDirectory() as temp:
                base = Path(temp)
                self.fixture(base)
                Image.new("RGB", (8, 6), "white").save(base / "candidate.png")
                (base / "criteria-source.txt").write_text("synthetic measured criteria source")
                criteria = {"status": "measured", "source": {"path": "criteria-source.txt",
                    "sha256": measure._sha256(base / "criteria-source.txt")}, "max_mae_rgb": 255}
                (base / "criteria.json").write_text(json.dumps(criteria))
                before = {p.name: measure._sha256(p) for p in base.iterdir()}
                reports = base / "reports"
                reports.mkdir()
                report = reports / "report.json"
                extra = []
                if kind == "criteria_source":
                    report = base / "criteria-source.txt"
                elif kind == "report":
                    report.symlink_to(base / "sentinel.png")
                elif kind == "ancestor":
                    (reports / "link").symlink_to(base, target_is_directory=True)
                    report = reports / "link" / "escaped.json"
                else:
                    (reports / f"{kind}.full.png").symlink_to(base / "sentinel.png")
                    extra = [f"--{kind}", str(reports / kind)]
                completed = subprocess.run([sys.executable, str(ROOT / "tools/fidelity/measure.py"),
                    "compare", "--reference", str(base / "sentinel.png"),
                    "--candidate", str(base / "candidate.png"), "--reference-state", "synthetic",
                    "--candidate-state", "synthetic", "--criteria", str(base / "criteria.json"),
                    "--report", str(report), *extra], capture_output=True, text=True)
                self.assertEqual({name: measure._sha256(base / name) for name in before}, before)
                self.assertEqual(completed.returncode, 2, completed.stderr)
                self.assertNotIn("Traceback", completed.stderr)
                self.assertFalse((base / "escaped.json").exists())

    def test_source_snapshot_counts_unique_png_paths_and_hashes_all_evidence(self):
        with tempfile.TemporaryDirectory() as temp:
            base = Path(temp)
            manifest, source = self.fixture(base)
            Image.new("RGB", (8, 6), "black").save(base / "second.png")
            (base / "support.txt").write_text("synthetic support")
            manifest["artifacts"] = ["support.txt"]
            manifest["frames"].append({"file": "second.png", "timestamp_ms": 200, "region": [0, 0, 8, 6]})
            source.write_text(json.dumps(manifest))
            snapshot = integration._source_snapshot(manifest, source, expected_frames=2)
            self.assertEqual(len(snapshot), 4)
            self.assertEqual(snapshot[str(source.resolve())], measure._sha256(source))
            # Identical contents are allowed: paths, not content hashes, must differ.
            self.assertEqual(snapshot[str(base / "sentinel.png")], snapshot[str(base / "second.png")])
            (base / "support.txt").write_text("changed synthetic support")
            self.assertNotEqual(integration._source_snapshot(manifest, source, expected_frames=2), snapshot)
            for kind in ("duplicate", "symlink", "not_png", "count"):
                with self.subTest(kind=kind):
                    modified = copy.deepcopy(manifest)
                    if kind == "duplicate":
                        modified["frames"][1]["file"] = "sentinel.png"
                    elif kind == "symlink":
                        (base / "alias.png").symlink_to(base / "sentinel.png")
                        modified["frames"][1]["file"] = "alias.png"
                    elif kind == "not_png":
                        Image.new("RGB", (8, 6)).save(base / "second.png", format="JPEG")
                    else:
                        modified["frames"].pop()
                    with self.assertRaisesRegex(ValueError, "PNG|frame"):
                        integration._source_snapshot(modified, source, expected_frames=2)

    def test_l4_malformed_timestamp_provenance_is_controlled_in_validator_and_cli(self):
        valid = {"field": "timestamp_ms", "source_value": 100, "source_unit": "ms"}
        invalid = [None, [], "ms", {}, {"field": "timestamp_ms"},
                   {**valid, "field": []}, {**valid, "field": "invented"},
                   {**valid, "source_unit": "ns"}, {**valid, "source_unit": None},
                   {**valid, "source_value": "100"}, {**valid, "source_value": False},
                   {**valid, "source_value": float("inf")},
                   {**valid, "source_value": float("nan")}, {**valid, "source_value": 101}]
        with tempfile.TemporaryDirectory() as temp:
            base = Path(temp)
            original, source = self.fixture(base)
            for value in invalid:
                with self.subTest(value=value):
                    manifest = copy.deepcopy(original)
                    manifest["frames"][0]["timestamp_provenance"] = value
                    with self.assertRaisesRegex(ValueError, "timestamp_provenance"):
                        measure.validate_manifest(manifest, base)
                    source.write_text(json.dumps(manifest))
                    report = base / "report.json"
                    completed = self.cli(source, report)
                    self.assertEqual(completed.returncode, 2, completed.stderr)
                    self.assertIn("timestamp_provenance", completed.stderr)
                    self.assertNotIn("Traceback", completed.stderr)
                    self.assertFalse(report.exists())
            for field, timestamp in (("timestamp_ms", 100), ("elapsed_ms", 100), ("monotonic_ns", 100000000)):
                manifest = copy.deepcopy(original)
                frame = manifest["frames"][0]
                frame.pop("timestamp_ms")
                frame[field] = timestamp
                first = measure.validate_manifest(manifest, base)
                self.assertEqual(measure.validate_manifest(first["normalized_manifest"], base), first)
                canonical = copy.deepcopy(first["normalized_manifest"])
                if field != "timestamp_ms":
                    canonical["frames"][0].pop(field)
                result = measure.validate_manifest(canonical, base)
                self.assertEqual(result["normalized_manifest"]["frames"][0]["timestamp_provenance"]["field"], field)
                self.assertEqual(measure.validate_manifest(result["normalized_manifest"], base), result)
                source.write_text(json.dumps(canonical))
                completed = self.cli(source, base / "valid-report.json")
                self.assertEqual(completed.returncode, 0, completed.stderr)

    def test_l3_cli_raw_named_and_normalized_events_are_equivalent(self):
        with tempfile.TemporaryDirectory() as temp:
            base = Path(temp)
            manifest, source = self.fixture(base)
            Image.new("RGB", (8, 6), "white").save(base / "changed.png")
            manifest["frames"].append({"file": "changed.png", "timestamp_ms": 200, "region": [0, 0, 8, 6]})
            events = [{"label": "click", "timestamp_ms": 150}]
            manifest["cases"][0].update(status="measured", artifacts=["sentinel.png"],
                                       measurements={"input_events": events})
            normalized = measure.validate_manifest(manifest, base)["normalized_manifest"]
            twice = measure.validate_manifest(normalized, base)["normalized_manifest"]
            named = copy.deepcopy(manifest)
            named["cases"][0]["measurements"] = [{"name": "input_events", "value": events}]
            reports = []
            for value in (manifest, normalized, twice, named):
                source.write_text(json.dumps(value))
                report = base / "report.json"
                completed = self.cli(source, report)
                self.assertEqual(completed.returncode, 0, completed.stderr)
                data = json.loads(report.read_text())
                data.pop("generated_utc")
                reports.append(data)
            expected = [{"label": "click", "event_timestamp_ms": 150, "event_bracket_ms": [100, 200],
                         "next_observed_change_interval_ms": [100, 200], "next_observed_change_frame": 1}]
            self.assertEqual(reports[0]["event_associations"], {"synthetic": expected})
            for report in reports[1:]:
                self.assertEqual(report["event_associations"], reports[0]["event_associations"],
                                 "event_associations differ after normalization")
                self.assertEqual(report, reports[0])
            self.assertEqual(reports[0]["equivalence"], "not_evaluated")

    def test_l2_raw_name_metadata_keeps_zero_false_and_categories(self):
        with tempfile.TemporaryDirectory() as temp:
            base = Path(temp)
            self.fixture(base)
            for values in ({"duration_ms": 0, "held": False}, {"duration_ms": 0},
                           {"held": False}, {"state": "idle"}):
                manifest = synthetic_manifest()
                manifest["cases"][0].update(status="measured", artifacts=["sentinel.png"],
                    measurements={"name": "cronometro", **values})
                with self.subTest(values=values):
                    first = measure.validate_manifest(manifest, base)
                    self.assertEqual(first, measure.validate_manifest(first["normalized_manifest"], base))
                    records = first["normalized_manifest"]["cases"][0]["measurements"]
                    self.assertEqual({r["name"]: r["value"] for r in records}, {"name": "cronometro", **values})
            for values in ({"name": "cronometro", "unit": "ms"},
                           {"name": "cronometro", "value": None},
                           {"name": "cronometro", "duration_ms": None}):
                manifest["cases"][0]["measurements"] = values
                with self.subTest(values=values), self.assertRaisesRegex(ValueError, "measurement"):
                    measure.validate_manifest(manifest, base)
            for value in (float("inf"), float("nan"), float("-inf")):
                manifest["cases"][0]["measurements"] = {"name": "cronometro", "duration_ms": 0, "bad": value}
                with self.subTest(value=value), self.assertRaisesRegex(ValueError, "finite"):
                    measure.validate_manifest(manifest, base)

    def test_l1_provenance_cannot_launder_reported_refresh(self):
        for claimed in ({}, synthetic_manifest()["environment"],
                        {"refresh_hz": 60}, {**synthetic_manifest()["environment"], "dpi": 120}):
            manifest = synthetic_manifest()
            manifest["environment"].pop("refresh_hz")
            manifest["environment"]["refresh_rate_hz_reported"] = 60
            manifest["provenance"] = {"producer_environment": copy.deepcopy(claimed)}
            with self.subTest(claimed=claimed):
                try:
                    first = measure.validate_manifest(manifest)
                except ValueError as exc:
                    self.assertIn("producer_environment", str(exc))
                else:
                    self.assertIn("refresh_hz", first["quality_gaps"])
                    self.assertFalse(first["equality_eligible"])
                    self.assertEqual(measure.validate_manifest(first["normalized_manifest"]), first)
        manifest = synthetic_manifest()
        manifest["provenance"] = {"producer_environment": {
            **manifest["environment"], "dpi": 120}}
        with self.assertRaisesRegex(ValueError, "producer_environment"):
            measure.validate_manifest(manifest)

    def test_s1_actual_integration_guards_both_json_destinations(self):
        for name, kind in (("validation", "final"), ("timeline", "final"),
                           ("validation", "hardlink"), ("validation", "ancestor"),
                           ("validation", "duplicate"), ("validation", "not_png")):
            with self.subTest(name=name, kind=kind), tempfile.TemporaryDirectory() as temp:
                base = Path(temp)
                evidence = base / "evidence"
                run = evidence / "20261031T0022-0300"
                run.mkdir(parents=True)
                manifest = synthetic_manifest()
                manifest["environment"].pop("refresh_hz")
                manifest["environment"]["refresh_rate_hz_reported"] = 60
                manifest["frames"] = []
                for index in range(152):
                    filename = f"synthetic-{index}.png"
                    Image.new("RGB", (8, 6), "black").save(run / filename)
                    manifest["frames"].append({"file": filename, "timestamp_ms": index,
                        "region": {"x": 0, "y": 0, "width": 8, "height": 6}})
                if kind == "duplicate":
                    manifest["frames"][1]["file"] = "synthetic-0.png"
                elif kind == "not_png":
                    Image.new("RGB", (8, 6), "black").save(run / "synthetic-1.png", format="JPEG")
                (run / "manifest.json").write_text(json.dumps(manifest))
                before = {p.name: measure._sha256(p) for p in run.iterdir()}
                reports = base / "reports"
                reports.mkdir()
                sentinel = run / "synthetic-0.png"
                if kind == "ancestor":
                    (base / "linked").symlink_to(reports, target_is_directory=True)
                    reports = base / "linked"
                elif kind in ("final", "hardlink"):
                    target = reports / f"real-152-{name}.json"
                    if kind == "final":
                        target.symlink_to(sentinel)
                    else:
                        target.hardlink_to(sentinel)
                case = integration.RealEvidenceIntegrationTests(
                    "test_real_152_pngs_96_dpi_adapter_and_cli_report")
                try:
                    with mock.patch.object(integration, "EVIDENCE_ROOT", str(evidence)), \
                         mock.patch.dict("os.environ", {"FIDELITY_EVIDENCE_REPORT_DIR": str(reports)}):
                        with self.assertRaisesRegex(ValueError, "output|PNG"):
                            case.test_real_152_pngs_96_dpi_adapter_and_cli_report()
                finally:
                    self.assertEqual({p.name: measure._sha256(p) for p in run.iterdir()}, before,
                                     "integration modified synthetic evidence")
                self.assertFalse((reports / "real-152-timeline.json").is_file()
                                 and name == "validation", "must preflight both outputs before writing")


if __name__ == "__main__":
    unittest.main()
