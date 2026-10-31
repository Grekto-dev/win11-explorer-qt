#!/usr/bin/env python3
"""Auditable, threshold-free measurements for Explorer fidelity evidence."""
from __future__ import annotations

import argparse
import json
import math
import platform
import sys
from datetime import datetime, timezone
from pathlib import Path
from typing import Any

from PIL import Image, ImageChops, ImageEnhance, ImageFilter, ImageStat, __version__ as PILLOW_VERSION


def _crop_box(crop: tuple[int, int, int, int] | None, size: tuple[int, int]):
    if crop is None:
        return (0, 0, *size)
    x, y, width, height = crop
    if any(type(v) is not int for v in crop) or x < 0 or y < 0 or width <= 0 or height <= 0:
        raise ValueError("crop must be integer x,y,width,height with positive dimensions")
    if x + width > size[0] or y + height > size[1]:
        raise ValueError("crop lies outside image")
    return x, y, x + width, y + height


def compare_region(reference: Image.Image, candidate: Image.Image,
                   crop: tuple[int, int, int, int] | None):
    if reference.size != candidate.size and crop is None:
        raise ValueError(f"dimensions differ: {reference.size} vs {candidate.size}; provide an explicit crop")
    # A supplied crop makes the compared coordinate rectangle explicit; validate against both images.
    box = _crop_box(crop, reference.size)
    if box[2] > candidate.width or box[3] > candidate.height:
        raise ValueError("crop lies outside candidate image")
    ref = reference.convert("RGBA").crop(box)
    cand = candidate.convert("RGBA").crop(box)
    diff = ImageChops.difference(ref, cand)
    channels = diff.split()
    pixels = ref.width * ref.height
    rgb_bytes = [ch.tobytes() for ch in channels[:3]]
    alpha_bytes = channels[3].tobytes()
    rgb_sum = sum(sum(values) for values in rgb_bytes)
    alpha_sum = sum(alpha_bytes)
    # Edge-mask disagreement quantifies contour change without claiming semantic alignment.
    def edge_mask(img):
        edges = img.convert("RGB").convert("L").filter(ImageFilter.FIND_EDGES)
        return edges.point(lambda n: 255 if n >= 32 else 0)
    ref_edges, cand_edges = edge_mask(ref), edge_mask(cand)
    edge_diff = ImageChops.difference(ref_edges, cand_edges)
    changed_edges = sum(1 for px in edge_diff.tobytes() if px)
    max_edges = sum(1 for px in ref_edges.tobytes() if px) + sum(1 for px in cand_edges.tobytes() if px)
    mae = rgb_sum / (pixels * 3)
    return {
        "geometry": {"x": box[0], "y": box[1], "width": ref.width, "height": ref.height},
        "color": {"mae_rgb": mae, "max_abs_rgb": max(max(values, default=0) for values in rgb_bytes),
                  "changed_rgb_pixels": sum(1 for i in range(pixels) if any(values[i] for values in rgb_bytes))},
        "alpha": {"mae": alpha_sum / pixels,
                  "changed_pixels": sum(1 for px in alpha_bytes if px)},
        "contour": {"threshold_luma_gradient": 32, "changed_pixels": changed_edges,
                    "changed_fraction_of_edge_pixels": changed_edges / max(1, max_edges)},
    }


def _resolve_artifact(value: str, base_dir: Path):
    if not isinstance(value, str) or not value:
        raise ValueError("artifact paths must be non-empty strings")
    base = base_dir.resolve()
    raw = Path(value)
    if raw.is_absolute():
        # Absolute producer paths are accepted only inside the published phase2 evidence tree.
        phase2_roots = [p for p in (base, *base.parents)
                        if p.name == "phase2" and p.parent.name == "reference"
                        and p.parent.parent.name == "fidelity" and p.parent.parent.parent.name == ".state"]
        if not phase2_roots:
            raise ValueError("absolute artifact paths require a phase2 evidence directory")
        resolved = raw.resolve()
        if not any(resolved.is_relative_to(root.resolve()) for root in phase2_roots):
            raise ValueError("artifact path escapes authorized phase2 evidence root")
    else:
        resolved = (base / raw).resolve()
        if not resolved.is_relative_to(base):
            raise ValueError("artifact path escapes manifest directory")
    if not resolved.is_file():
        raise ValueError(f"evidence artifact not found: {value}")
    return resolved


def _has_measured_value(value: Any):
    if value is None:
        return False
    if isinstance(value, bool):
        return True
    if isinstance(value, (int, float)):
        return math.isfinite(value)
    if isinstance(value, str):
        text = value.strip().lower()
        return bool(text) and not any(marker in text for marker in ("unknown", "not measured", "not observed"))
    if isinstance(value, dict):
        return bool(value) and any(_has_measured_value(item) for item in value.values())
    if isinstance(value, list):
        return bool(value) and any(_has_measured_value(item) for item in value)
    return False


def _normalize_manifest_frames(frames: list[Any]):
    normalized = []
    for frame in frames:
        if not isinstance(frame, dict):
            raise ValueError("each frame must be an object")
        if "timestamp_ms" in frame:
            field, unit, timestamp = "timestamp_ms", "ms", frame["timestamp_ms"]
        elif "elapsed_ms" in frame:
            field, unit, timestamp = "elapsed_ms", "ms", frame["elapsed_ms"]
        elif "monotonic_ns" in frame:
            field, unit, timestamp = "monotonic_ns", "ns", frame["monotonic_ns"]
        else:
            raise ValueError("manifest frames require timestamp_ms, elapsed_ms, or monotonic_ns")
        if isinstance(timestamp, bool) or not isinstance(timestamp, (int, float)) or not math.isfinite(timestamp):
            raise ValueError("frame timestamps must be finite numbers")
        if unit == "ns":
            timestamp = timestamp / 1_000_000
        region = frame.get("region", frame.get("region_px"))
        normalized.append({**frame, "timestamp_ms": timestamp, "region": region,
                           "timestamp_provenance": {"field": field, "source_value": frame[field], "source_unit": unit}})
    return normalized


def _validate_frame_order(frames: list[dict[str, Any]]):
    times = [frame["timestamp_ms"] for frame in frames]
    if any(right <= left for left, right in zip(times, times[1:])):
        raise ValueError("timestamps must be strictly increasing")
    for frame in frames:
        if not isinstance(frame.get("file"), str) or not frame["file"]:
            raise ValueError("each frame requires a file")


def adapt_transfer_timeline(data: dict[str, Any], base_dir: Path):
    """Adapt the producer's transfer log without changing measured elapsed_ms."""
    frames = data.get("frames")
    if not isinstance(frames, list) or not frames:
        raise ValueError("transfer timeline requires a non-empty frames list")
    adapted = []
    for frame in frames:
        if not isinstance(frame, dict) or "elapsed_ms" not in frame:
            raise ValueError("transfer frame requires measured elapsed_ms")
        path = _resolve_artifact(frame.get("file", ""), base_dir)
        region = frame.get("region_px")
        if region is None:
            # The published log omits an ROI; use the actual whole-image bounds, not a guessed client rect.
            with Image.open(path) as image:
                region = [0, 0, image.width, image.height]
        if not isinstance(region, list) or len(region) != 4:
            raise ValueError("transfer frame region_px must contain four values")
        adapted.append({"file": str(path), "timestamp_ms": frame["elapsed_ms"],
                        "region": region, "label": frame.get("label")})
    return {"frames": adapted, "timestamp_provenance": "transfer-ui-timeline.json elapsed_ms",
            "region_provenance": "producer region_px or full-frame image dimensions when region_px absent",
            "source_start_utc": data.get("start_utc")}


def measure_timeline(frames: list[dict[str, Any]], base_dir: Path | None = None):
    if not frames:
        raise ValueError("timeline requires at least one frame")
    times = []
    for frame in frames:
        t = frame.get("timestamp_ms")
        if isinstance(t, bool) or not isinstance(t, (int, float)) or not math.isfinite(t):
            raise ValueError("timestamps must be finite monotonic milliseconds")
        times.append(float(t))
        if not isinstance(frame.get("file"), str) or not frame["file"]:
            raise ValueError("each frame requires a file")
    intervals = [b - a for a, b in zip(times, times[1:])]
    if any(d <= 0 for d in intervals):
        raise ValueError("timestamps must be strictly increasing")
    curve = []
    previous = None
    first_change = last_change = None
    for index, frame in enumerate(frames):
        path = _resolve_artifact(frame["file"], base_dir) if base_dir is not None else Path(frame["file"])
        with Image.open(path) as source:
            image = source.convert("RGB")
        region = frame.get("region")
        box = _crop_box(tuple(region) if region is not None else None, image.size)
        image = image.crop(box)
        if previous is not None and image.size != previous.size:
            raise ValueError("timeline frame ROI dimensions differ")
        if previous is None:
            mae, changed = 0.0, 0.0
        else:
            diff = ImageChops.difference(previous, image)
            mae = sum(ImageStat.Stat(diff).mean) / 3
            diff_bytes = diff.tobytes()
            changed = sum(1 for i in range(0, len(diff_bytes), 3)
                          if diff_bytes[i] or diff_bytes[i + 1] or diff_bytes[i + 2]) / (image.width * image.height)
            if changed > 0:
                if first_change is None:
                    first_change = [times[index - 1], times[index]]
                last_change = [times[index - 1], times[index]]
        curve.append({"timestamp_ms": times[index], "elapsed_ms": times[index] - times[0],
                      "frame": index, "mae_rgb_from_previous": mae, "changed_fraction": changed})
        previous = image
    return {"frame_count": len(frames), "start_ms": times[0], "end_ms": times[-1],
            "duration_ms": times[-1] - times[0], "observed_intervals_ms": intervals,
            "curve": curve, "first_change_interval_ms": first_change,
            "last_change_interval_ms": last_change,
            "timing_precision": "observed capture timestamps; visual change is bracketed by sampled frames; no interpolation or inferred sub-cadence duration"}


def evaluate_criteria(metrics: dict[str, Any], criteria: dict[str, Any] | None,
                      base_dir: Path | None = None):
    if criteria is None:
        return {"status": "not_evaluated", "equivalence": "not_evaluated",
                "reason": "no explicit measured criteria supplied"}
    if criteria.get("status") != "measured":
        raise ValueError("criteria must be explicitly marked measured and cite a source")
    source = criteria.get("source")
    if not isinstance(source, dict) or not isinstance(source.get("path"), str) or not isinstance(source.get("sha256"), str):
        raise ValueError("criteria source evidence must provide path and sha256")
    if base_dir is None:
        raise ValueError("criteria source evidence cannot be verified without a base directory")
    source_path = _resolve_artifact(source["path"], base_dir)
    if _sha256(source_path).lower() != source["sha256"].lower():
        raise ValueError("criteria source evidence sha256 mismatch")
    limits = {k[4:]: v for k, v in criteria.items() if k.startswith("max_")}
    if not limits:
        raise ValueError("measured criteria must provide at least one max_* metric")
    results = {}
    for metric, limit in limits.items():
        if metric not in metrics:
            raise ValueError(f"criterion references unavailable metric: {metric}")
        if isinstance(limit, bool) or not isinstance(limit, (int, float)) or not math.isfinite(limit) or limit < 0:
            raise ValueError(f"criterion limit for {metric} must be finite and non-negative")
        if isinstance(metrics[metric], bool) or not isinstance(metrics[metric], (int, float)) or not math.isfinite(metrics[metric]):
            raise ValueError(f"observed metric {metric} must be finite")
        results[metric] = {"observed": metrics[metric], "max": limit, "within": metrics[metric] <= limit}
    return {"status": "measured", "equivalence": "pass" if all(x["within"] for x in results.values()) else "fail",
            "source": criteria["source"], "criteria": results}


_ENV_ALIASES = {"locale": "language", "resolution": "resolution_px",
                "client_rect": "client_rect_px", "refresh_hz": "refresh_rate_hz_reported"}
_ENV_FIELDS = ("build", "theme", "locale", "dpi", "scale", "resolution", "client_rect", "refresh_hz", "capture_method")
_ALLOWED_STATUS = {"measured", "blocked", "not_observed"}


def validate_manifest(manifest: Any, base_dir: Path | None = None):
    if not isinstance(manifest, dict) or manifest.get("schema_version") != 1:
        raise ValueError("schema_version must be 1")
    env = manifest.get("environment")
    if not isinstance(env, dict):
        raise ValueError("environment must be an object")
    normalized_env = {**env, **{key: env.get(_ENV_ALIASES.get(key, key), "unknown") for key in _ENV_FIELDS}}
    quality_gaps = []
    for key in _ENV_FIELDS:
        value = normalized_env[key]
        if value is None or value == "unknown" or (isinstance(value, str) and any(word in value.lower() for word in ("unknown", "approx", "not measured", "not trusted", "not reconfirmed"))):
            quality_gaps.append(key)
    if not isinstance(normalized_env["resolution"], list) or len(normalized_env["resolution"]) != 2 or any(type(n) is not int or n <= 0 for n in normalized_env["resolution"]):
        quality_gaps.append("resolution")
    client_rect = normalized_env["client_rect"]
    if not isinstance(client_rect, list) or len(client_rect) != 4 or any(type(n) is not int for n in client_rect) or client_rect[0] < 0 or client_rect[1] < 0 or client_rect[2] <= 0 or client_rect[3] <= 0:
        if "client_rect" not in quality_gaps:
            quality_gaps.append("client_rect")
    for key in ("dpi", "scale", "refresh_hz"):
        value = normalized_env[key]
        if isinstance(value, bool) or not isinstance(value, (int, float)) or not math.isfinite(value) or value <= 0:
            if key not in quality_gaps:
                quality_gaps.append(key)
    # These producer fields explicitly report approximations/untrusted values.
    if "client_rect_px" in env and "approx" in str(env["client_rect_px"]).lower() and "client_rect" not in quality_gaps:
        quality_gaps.append("client_rect")
    if "refresh_rate_hz_reported" in env:
        if "refresh_hz" not in quality_gaps:
            quality_gaps.append("refresh_hz")
    quality_gaps = list(dict.fromkeys(quality_gaps))
    cases = manifest.get("cases")
    if not isinstance(cases, list) or not cases:
        raise ValueError("cases must be a non-empty list")
    ids = set()
    for case in cases:
        if not isinstance(case, dict) or not isinstance(case.get("id"), str) or not case["id"]:
            raise ValueError("each case requires id")
        if case["id"] in ids:
            raise ValueError("case ids must be unique")
        ids.add(case["id"])
        if case.get("status") not in _ALLOWED_STATUS:
            raise ValueError("case status must be measured, blocked, or not_observed")
        if not isinstance(case.get("artifacts"), list) or not isinstance(case.get("measurements"), (list, dict)) or not isinstance(case.get("limitations"), list):
            raise ValueError("case artifacts and limitations must be lists; measurements must be list or object")
        if case["status"] == "measured":
            if not case["artifacts"] or any(not isinstance(item, str) or not item for item in case["artifacts"]):
                raise ValueError("measured case must include at least one supporting artifact")
            measurements = case["measurements"]
            if not _has_measured_value(measurements):
                raise ValueError("measured case must include at least one measurement")
    raw_frames = manifest.get("frames", [])
    if not isinstance(raw_frames, list):
        raise ValueError("frames must be a list")
    frames = _normalize_manifest_frames(raw_frames)
    _validate_frame_order(frames)
    if frames and base_dir is not None:
        measure_timeline(frames, base_dir)
    for item in frames:
        region = item.get("region")
        if not isinstance(region, list) or len(region) != 4:
            raise ValueError("each frame requires a four-value region")
        x, y, width, height = region
        if any(type(value) is not int for value in region) or x < 0 or y < 0 or width <= 0 or height <= 0:
            raise ValueError("frame region must use non-negative integer x/y and positive integer width/height")
    if base_dir is not None:
        paths = []
        for case in cases:
            paths.extend(case["artifacts"])
        paths.extend(f["file"] for f in frames)
        for value in paths:
            _resolve_artifact(value, base_dir)
    normalized = dict(manifest)
    normalized["environment"] = normalized_env
    normalized["frames"] = frames
    normalized["cases"] = []
    for case in cases:
        normalized_case = dict(case)
        if isinstance(case["measurements"], dict):
            normalized_case["producer_measurements"] = dict(case["measurements"])
            normalized_case["measurements"] = [{"name": key, "value": value} for key, value in case["measurements"].items()]
        normalized["cases"].append(normalized_case)
    normalized["provenance"] = {"adapter": "schema-v1-alias-and-measurement-container-normalizer",
                                "producer_environment": dict(env),
                                "source_frames": raw_frames,
                                "source_schema_version": manifest["schema_version"]}
    return {"status": "valid", "schema_version": 1, "case_count": len(cases), "frame_count": len(frames),
            "environment": normalized_env, "quality_gaps": quality_gaps,
            "equality_eligible": not quality_gaps, "normalized_manifest": normalized}


def _parse_region(text: str):
    try:
        name, *coords = text.split(",")
        if len(coords) != 4 or not name:
            raise ValueError
        return name, tuple(int(c) for c in coords)
    except ValueError as exc:
        raise argparse.ArgumentTypeError("region format is NAME,X,Y,WIDTH,HEIGHT") from exc


def _compare(args):
    if args.reference_state != args.candidate_state:
        raise ValueError("reference and candidate state metadata are incompatible")
    if not args.reference_state:
        raise ValueError("state metadata is required")
    ref_path, cand_path = Path(args.reference).resolve(), Path(args.candidate).resolve()
    for p in (ref_path, cand_path):
        if not p.is_file():
            raise ValueError(f"evidence image not found: {p}")
    ref, cand = Image.open(ref_path), Image.open(cand_path)
    regions = args.region or [("full", None)]
    output_regions = []
    for name, crop in regions:
        result = compare_region(ref, cand, crop)
        result["name"] = name
        output_regions.append(result)
        if args.heatmap or args.overlay:
            box = _crop_box(crop, ref.size)
            r, c = ref.convert("RGBA").crop(box), cand.convert("RGBA").crop(box)
            diff = ImageChops.difference(r, c).convert("RGB")
            if args.heatmap:
                ImageEnhance.Contrast(diff).enhance(4).save(f"{args.heatmap}.{name}.png")
            if args.overlay:
                Image.blend(r.convert("RGB"), c.convert("RGB"), .5).save(f"{args.overlay}.{name}.png")
    report = {"schema_version": 1, "generated_utc": datetime.now(timezone.utc).isoformat(),
              "status": "not_evaluated", "equivalence": "not_evaluated",
              "reference": {"path": str(ref_path), "sha256": _sha256(ref_path), "state": args.reference_state},
              "candidate": {"path": str(cand_path), "sha256": _sha256(cand_path), "state": args.candidate_state},
              "tool_versions": {"python": platform.python_version(), "pillow": PILLOW_VERSION},
              "capture_metadata": {"reference_label": args.reference_label, "candidate_label": args.candidate_label},
              "regions": output_regions}
    if args.criteria:
        criteria = json.loads(Path(args.criteria).read_text())
        # Explicit criteria may evaluate named region metrics, never a hidden global aggregate.
        for region in output_regions:
            region["evaluation"] = evaluate_criteria(region["color"], criteria, Path(args.criteria).resolve().parent)
        evaluations = [r["evaluation"] for r in output_regions]
        report["status"] = "measured"
        report["equivalence"] = "pass" if all(e["equivalence"] == "pass" for e in evaluations) else "fail"
    Path(args.report).parent.mkdir(parents=True, exist_ok=True)
    Path(args.report).write_text(json.dumps(report, indent=2, sort_keys=True) + "\n")
    print(json.dumps({"report": str(Path(args.report).resolve()), "status": report["status"],
                      "equivalence": report["equivalence"], "regions": len(output_regions)}))


def _timeline(args):
    path = Path(args.manifest).resolve()
    if not path.is_file():
        raise ValueError(f"manifest not found: {path}")
    manifest = json.loads(path.read_text())
    transfer = None
    validation = None
    if "frames" in manifest and "start_utc" in manifest:
        transfer = adapt_transfer_timeline(manifest, path.parent)
        frames = transfer["frames"]
    else:
        validation = validate_manifest(manifest, path.parent)
        frames = validation["normalized_manifest"].get("frames", [])
        transfer = None
    if not frames:
        raise ValueError("manifest contains no frames to measure")
    manifest_validation = None
    if validation is not None:
        manifest_validation = {key: validation[key] for key in
                               ("status", "quality_gaps", "equality_eligible")}
    timestamp_provenance = transfer["timestamp_provenance"] if transfer else sorted({
        frame["timestamp_provenance"]["field"] for frame in frames})
    report = {"schema_version": 1, "generated_utc": datetime.now(timezone.utc).isoformat(),
              "status": "measured" if transfer is not None or any(c["status"] == "measured" for c in manifest.get("cases", [])) else "not_evaluated",
              "equivalence": "not_evaluated", "manifest": str(path),
              "manifest_validation": manifest_validation,
              "timestamp_provenance": timestamp_provenance,
              "region_provenance": transfer["region_provenance"] if transfer else "manifest region",
              "tool_versions": {"python": platform.python_version(),
              "pillow": PILLOW_VERSION}, "timelines": [measure_timeline(frames, path.parent)]}
    Path(args.report).parent.mkdir(parents=True, exist_ok=True)
    Path(args.report).write_text(json.dumps(report, indent=2, sort_keys=True) + "\n")
    print(json.dumps({"report": str(Path(args.report).resolve()), "status": report["status"],
                      "equivalence": report["equivalence"], "frames": len(frames)}))


def _sha256(path: Path):
    import hashlib
    h = hashlib.sha256()
    with path.open("rb") as f:
        for block in iter(lambda: f.read(1024 * 1024), b""):
            h.update(block)
    return h.hexdigest()


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    subs = parser.add_subparsers(dest="command", required=True)
    validate = subs.add_parser("validate", help="validate a shared evidence manifest and all referenced files")
    validate.add_argument("manifest")
    timeline = subs.add_parser("timeline", help="measure monotonic observed frame timestamps in a manifest")
    timeline.add_argument("manifest")
    timeline.add_argument("--report", required=True)
    compare = subs.add_parser("compare", help="measure images; no thresholds are assumed")
    compare.add_argument("--reference", required=True)
    compare.add_argument("--candidate", required=True)
    compare.add_argument("--reference-state", required=True)
    compare.add_argument("--candidate-state", required=True)
    compare.add_argument("--reference-label", default="unspecified")
    compare.add_argument("--candidate-label", default="unspecified")
    compare.add_argument("--region", action="append", type=_parse_region)
    compare.add_argument("--criteria", help="JSON with status=measured, source, and max_* limits")
    compare.add_argument("--heatmap", help="output path prefix")
    compare.add_argument("--overlay", help="output path prefix")
    compare.add_argument("--report", required=True)
    args = parser.parse_args(argv)
    try:
        if args.command == "validate":
            path = Path(args.manifest).resolve()
            if not path.is_file():
                raise ValueError(f"manifest not found: {path}")
            result = validate_manifest(json.loads(path.read_text()), path.parent)
            print(json.dumps({key: result[key] for key in
                              ("status", "schema_version", "case_count", "frame_count", "quality_gaps", "equality_eligible")}))
        elif args.command == "compare":
            _compare(args)
        else:
            _timeline(args)
        return 0
    except (ValueError, OSError, json.JSONDecodeError) as exc:
        print(f"fidelity: {exc}", file=sys.stderr)
        return 2


if __name__ == "__main__":
    raise SystemExit(main())
