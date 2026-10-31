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


def measure_timeline(frames: list[dict[str, Any]]):
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
    return {"frame_count": len(frames), "start_ms": times[0], "end_ms": times[-1],
            "duration_ms": times[-1] - times[0], "observed_intervals_ms": intervals,
            "curve": [{"timestamp_ms": t, "elapsed_ms": t - times[0], "frame": i}
                      for i, t in enumerate(times)],
            "timing_precision": "observed capture timestamps; no interpolated frames or inferred frame rate"}


def evaluate_criteria(metrics: dict[str, Any], criteria: dict[str, Any] | None):
    if criteria is None:
        return {"status": "not_evaluated", "equivalence": "not_evaluated",
                "reason": "no explicit measured criteria supplied"}
    if criteria.get("status") != "measured" or not criteria.get("source"):
        raise ValueError("criteria must be explicitly marked measured and cite a source")
    limits = {k[4:]: v for k, v in criteria.items() if k.startswith("max_")}
    if not limits:
        raise ValueError("measured criteria must provide at least one max_* metric")
    results = {}
    for metric, limit in limits.items():
        if metric not in metrics:
            raise ValueError(f"criterion references unavailable metric: {metric}")
        if isinstance(limit, bool) or not isinstance(limit, (int, float)) or limit < 0:
            raise ValueError(f"invalid criterion limit for {metric}")
        results[metric] = {"observed": metrics[metric], "max": limit, "within": metrics[metric] <= limit}
    return {"status": "measured", "equivalence": "pass" if all(x["within"] for x in results.values()) else "fail",
            "source": criteria["source"], "criteria": results}


_REQUIRED_ENV = {"build", "theme", "locale", "dpi", "scale", "resolution", "client_rect", "refresh_hz", "capture_method"}
_ALLOWED_STATUS = {"measured", "blocked", "not_observed"}


def validate_manifest(manifest: Any, base_dir: Path | None = None):
    if not isinstance(manifest, dict) or manifest.get("schema_version") != 1:
        raise ValueError("schema_version must be 1")
    env = manifest.get("environment")
    if not isinstance(env, dict) or not _REQUIRED_ENV.issubset(env):
        raise ValueError("environment missing required metadata")
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
        if not isinstance(case.get("artifacts"), list) or not isinstance(case.get("measurements"), list) or not isinstance(case.get("limitations"), list):
            raise ValueError("case artifacts, measurements, and limitations must be lists")
    frames = manifest.get("frames", [])
    if not isinstance(frames, list):
        raise ValueError("frames must be a list")
    if frames:
        measure_timeline(frames)
    for item in frames:
        region = item.get("region")
        if not isinstance(region, list) or len(region) != 4:
            raise ValueError("each frame requires a four-value region")
    if base_dir is not None:
        paths = []
        for case in cases:
            paths.extend(case["artifacts"])
        paths.extend(f["file"] for f in frames)
        for value in paths:
            p = (base_dir / value).resolve()
            if not p.is_relative_to(base_dir.resolve()):
                raise ValueError("artifact path escapes manifest directory")
            if not p.is_file():
                raise ValueError(f"evidence artifact not found: {value}")
    return {"status": "valid", "schema_version": 1, "case_count": len(cases), "frame_count": len(frames)}


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
            region["evaluation"] = evaluate_criteria(region["color"], criteria)
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
    validation = validate_manifest(manifest, path.parent)
    frames = manifest.get("frames", [])
    if not frames:
        raise ValueError("manifest contains no frames to measure")
    report = {"schema_version": 1, "generated_utc": datetime.now(timezone.utc).isoformat(),
              "status": "measured" if any(c["status"] == "measured" for c in manifest["cases"]) else "not_evaluated",
              "equivalence": "not_evaluated", "manifest": str(path),
              "manifest_validation": validation, "tool_versions": {"python": platform.python_version(),
              "pillow": PILLOW_VERSION}, "timelines": [measure_timeline(frames)]}
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
            print(json.dumps(result))
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
