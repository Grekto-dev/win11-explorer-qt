#!/usr/bin/env python3
"""Auditable, threshold-free measurements for Explorer fidelity evidence."""
from __future__ import annotations

import argparse
import json
import math
import platform
import sys
import os
import stat
import secrets
from io import BytesIO
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


_MEASUREMENT_METADATA = {"name", "id", "label", "unit", "units", "description", "source", "provenance", "limitations"}


def _validate_measurement_numbers(value: Any):
    # Traverse every leaf before searching for an observation: any() must not
    # let an earlier valid value hide NaN/Infinity in a neighbouring measurement.
    if isinstance(value, float) and not math.isfinite(value):
        raise ValueError("measurement numbers must be finite")
    if isinstance(value, dict):
        for item in value.values():
            _validate_measurement_numbers(item)
    elif isinstance(value, list):
        for item in value:
            _validate_measurement_numbers(item)


def _contains_observed_value(value: Any):
    if value is None:
        return False
    if isinstance(value, (bool, int, float)):
        return True  # Finite numbers were checked separately; zero/False are data.
    if isinstance(value, str):
        text = value.strip().lower().replace("_", " ").replace("-", " ")
        return bool(text) and text not in {"null", "none", "n/a", "nan", "infinity"} and not any(
            marker in text for marker in ("unknown", "not measured", "not observed", "unmeasured"))
    if isinstance(value, dict):
        return any(_contains_observed_value(item) for key, item in value.items()
                   if key not in _MEASUREMENT_METADATA)
    if isinstance(value, list):
        return any(_contains_observed_value(item) for item in value)
    return False


def _has_measured_value(value: Any):
    """Inspect observed payloads, never metric names/units/descriptions."""
    _validate_measurement_numbers(value)
    if isinstance(value, list):
        # The container defines named records; auxiliary fields are not payloads.
        return any(_contains_observed_value(record.get("value"))
                   for record in value if isinstance(record, dict))
    return _contains_observed_value(value)


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
        extra = {}
        if isinstance(region, dict):
            if set(region) != {"x", "y", "width", "height"}:
                raise ValueError("screen-space frame region must contain exactly x, y, width, and height")
            origin = [region["x"], region["y"]]
            width, height = region["width"], region["height"]
            values = [*origin, width, height]
            if any(type(value) is not int for value in values) or origin[0] < 0 or origin[1] < 0 or width <= 0 or height <= 0:
                raise ValueError("screen-space frame region must use non-negative integer origin and positive integer dimensions")
            region = [0, 0, width, height]
            extra = {"screen_origin": origin, "region_coordinate_space": "screen"}
        elif isinstance(region, list):
            extra = {"region_coordinate_space": frame.get("region_coordinate_space", "image")}
        timestamp_provenance = frame.get("timestamp_provenance", {
            "field": field, "source_value": frame[field], "source_unit": unit})
        if not isinstance(timestamp_provenance, dict) or set(timestamp_provenance) != {"field", "source_value", "source_unit"}:
            raise ValueError("timestamp_provenance must contain field, source_value and source_unit")
        source_field = timestamp_provenance["field"]
        units = {"timestamp_ms": "ms", "elapsed_ms": "ms", "monotonic_ns": "ns"}
        if not isinstance(source_field, str) or source_field not in units or timestamp_provenance["source_unit"] != units[source_field]:
            raise ValueError("timestamp_provenance field and source_unit must name a supported timestamp")
        source_value = timestamp_provenance["source_value"]
        if type(source_value) not in (int, float) or not math.isfinite(source_value):
            raise ValueError("timestamp_provenance source_value must be finite numeric data")
        source_ms = source_value / 1_000_000 if units[source_field] == "ns" else source_value
        if source_ms != timestamp or (source_field in frame and frame[source_field] != source_value):
            raise ValueError("timestamp_provenance does not match frame timestamp")
        normalized.append({**frame, **extra, "timestamp_ms": timestamp, "region": region,
                           "timestamp_provenance": timestamp_provenance})
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


def associate_timeline_events(timeline: dict[str, Any], events: list[dict[str, Any]]):
    """Bracket input events against samples and report the next sampled ROI change."""
    curve = timeline["curve"]
    associated = []
    for event in events:
        timestamp = event.get("timestamp_ms")
        if isinstance(timestamp, bool) or not isinstance(timestamp, (int, float)) or not math.isfinite(timestamp):
            raise ValueError("event timestamps must be finite milliseconds")
        right = next((i for i, point in enumerate(curve) if point["timestamp_ms"] >= timestamp), None)
        bracket = None
        if right is not None:
            bracket = ([curve[max(0, right - 1)]["timestamp_ms"], curve[right]["timestamp_ms"]])
        changed = next((i for i in range(max(1, right or 1), len(curve))
                        if curve[i]["changed_fraction"] > 0), None) if right is not None else None
        associated.append({"label": event.get("label"), "event_timestamp_ms": timestamp,
                           "event_bracket_ms": bracket,
                           "next_observed_change_interval_ms": ([curve[changed - 1]["timestamp_ms"], curve[changed]["timestamp_ms"]]
                                                                 if changed is not None else None),
                           "next_observed_change_frame": changed})
    return associated


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
_ALLOWED_STATUS = {"measured", "partial", "blocked", "not_observed"}


def _normalize_environment(env):
    normalized_env = {**env, **{key: env[key] if key in env else env.get(_ENV_ALIASES.get(key, key), "unknown")
                              for key in _ENV_FIELDS}}
    if "refresh_hz" not in env and "refresh_rate_hz" in env:
        normalized_env["refresh_hz"] = env["refresh_rate_hz"] if env["refresh_rate_hz"] is not None else "unknown"
    if isinstance(env.get("dpi"), dict):
        measured_dpi = env["dpi"].get("GetDpiForWindow")
        measured_scale = env["dpi"].get("scale_percent")
        normalized_env["dpi"] = measured_dpi if type(measured_dpi) in (int, float) else "unknown"
        if "scale" not in env:
            normalized_env["scale"] = measured_scale / 100 if type(measured_scale) in (int, float) else "unknown"
    if "client_rect" not in env and isinstance(env.get("client_rect_px"), dict):
        rect = env["client_rect_px"].get("client_rect")
        normalized_env["client_rect"] = rect if isinstance(rect, list) else "unknown"
    return normalized_env


def validate_manifest(manifest: Any, base_dir: Path | None = None):
    if not isinstance(manifest, dict) or manifest.get("schema_version") != 1:
        raise ValueError("schema_version must be 1")
    env = manifest.get("environment")
    if not isinstance(env, dict):
        raise ValueError("environment must be an object")
    provenance = manifest.get("provenance", {})
    if not isinstance(provenance, dict):
        raise ValueError("provenance must be an object")
    source_env = provenance.get("producer_environment", env)
    if not isinstance(source_env, dict):
        raise ValueError("provenance producer_environment must be an object")
    normalized_env = _normalize_environment(env)
    source_normalized = _normalize_environment(source_env)
    if source_normalized != normalized_env:
        raise ValueError("provenance producer_environment does not match effective environment")
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
    # Neither claimed provenance nor a normalized canonical alias can erase
    # the producer's explicitly reported/approximate quality limitation.
    for producer in (env, source_env):
        if "client_rect" not in producer and "approx" in str(producer.get("client_rect_px", "")).lower():
            quality_gaps.append("client_rect")
        if "refresh_hz" not in producer and "refresh_rate_hz" not in producer and "refresh_rate_hz_reported" in producer:
            quality_gaps.append("refresh_hz")
    quality_gaps = list(dict.fromkeys(quality_gaps))
    cases = manifest.get("cases")
    if not isinstance(cases, list) or not cases:
        raise ValueError("cases must be a non-empty list")
    artifacts = manifest.get("artifacts", [])
    if not isinstance(artifacts, list) or any(not isinstance(item, str) or not item for item in artifacts):
        raise ValueError("manifest artifacts must be a list of non-empty path strings")
    ids = set()
    for case in cases:
        if not isinstance(case, dict) or not isinstance(case.get("id"), str) or not case["id"]:
            raise ValueError("each case requires id")
        if case["id"] in ids:
            raise ValueError("case ids must be unique")
        ids.add(case["id"])
        if case.get("status") not in _ALLOWED_STATUS:
            raise ValueError("case status must be measured, partial, blocked, or not_observed")
        if not isinstance(case.get("artifacts"), list) or not isinstance(case.get("measurements"), (list, dict)) or not isinstance(case.get("limitations"), list):
            raise ValueError("case artifacts and limitations must be lists; measurements must be list or object")
        has_observation = _has_measured_value(case["measurements"])
        if case["status"] == "measured":
            if not case["artifacts"] or any(not isinstance(item, str) or not item for item in case["artifacts"]):
                raise ValueError("measured case must include at least one supporting artifact")
            if not has_observation:
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
        paths = list(artifacts)
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
                                "source_schema_version": manifest["schema_version"], **provenance}
    return {"status": "valid", "schema_version": 1, "case_count": len(cases), "frame_count": len(frames),
            "environment": normalized_env, "quality_gaps": quality_gaps,
            "equality_eligible": not quality_gaps, "normalized_manifest": normalized}


def _check_output_paths(paths, protected_paths=(), protected_roots=()):
    """Preflight every destination, including existing physical components."""
    sources = [Path(p).resolve() for p in protected_paths]
    roots = [Path(p).resolve() for p in protected_roots]
    for source in sources:
        roots.extend(p for p in source.parents if p.name == "phase2"
                     and p.parent.name == "reference" and p.parent.parent.name == "fidelity"
                     and p.parent.parent.parent.name == ".state")
    destinations = []
    for value in paths:
        path = Path(value).absolute()
        if ".." in path.parts:
            raise ValueError("output path must not contain parent traversal")
        for component in (path, *path.parents):
            if component.is_symlink():
                raise ValueError("output path must not contain symlinks")
        physical = path.resolve()
        if any(physical == previous or (path.exists() and previous.exists()
               and os.path.samefile(path, previous)) for previous in destinations):
            raise ValueError("output paths collide with another planned destination")
        destinations.append(physical)
        if any(physical.is_relative_to(root) for root in roots):
            raise ValueError("output path overlaps protected evidence root")
        if physical in sources or (path.exists() and any(
                source.exists() and os.path.samefile(path, source) for source in sources)):
            raise ValueError("output path overlaps source evidence")
        if path.exists():
            info = path.stat(follow_symlinks=False)
            if not stat.S_ISREG(info.st_mode) or info.st_nlink != 1:
                raise ValueError("output must be an unlinked regular file")


def _write_output(path, payload, protected_paths=(), protected_roots=()):
    """Atomic replacement through pinned no-follow directory descriptors."""
    _check_output_paths([path], protected_paths, protected_roots)
    path = Path(path).absolute()
    directory = os.open(path.anchor, os.O_RDONLY | os.O_DIRECTORY | os.O_NOFOLLOW)
    temporary = None
    try:
        for part in path.parts[1:-1]:
            try:
                os.mkdir(part, dir_fd=directory)
            except FileExistsError:
                pass
            child = os.open(part, os.O_RDONLY | os.O_DIRECTORY | os.O_NOFOLLOW, dir_fd=directory)
            os.close(directory)
            directory = child
        try:
            info = os.stat(path.name, dir_fd=directory, follow_symlinks=False)
        except FileNotFoundError:
            info = None
        if info is not None and (not stat.S_ISREG(info.st_mode) or info.st_nlink != 1):
            raise ValueError("output must be a non-symlink, unlinked regular file")
        temporary = ".fidelity-" + secrets.token_hex(16)
        fd = os.open(temporary, os.O_WRONLY | os.O_CREAT | os.O_EXCL | os.O_NOFOLLOW,
                     0o600, dir_fd=directory)
        with os.fdopen(fd, "wb") as stream:
            stream.write(payload)
            stream.flush()
            os.fsync(stream.fileno())
        # rename replaces the directory entry itself, never follows a raced-in symlink.
        os.replace(temporary, path.name, src_dir_fd=directory, dst_dir_fd=directory)
        temporary = None
    finally:
        if temporary is not None:
            os.unlink(temporary, dir_fd=directory)
        os.close(directory)


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
    protected = [ref_path, cand_path]
    criteria = None
    if args.criteria:
        criteria_path = Path(args.criteria).resolve()
        protected.append(criteria_path)
        criteria = json.loads(criteria_path.read_text())
        source = criteria.get("source") if isinstance(criteria, dict) else None
        if not isinstance(source, dict):
            raise ValueError("criteria source evidence must provide path and sha256")
        source_path = source.get("path")
        if not isinstance(source_path, str):
            raise ValueError("criteria source evidence must provide path and sha256")
        protected.append(_resolve_artifact(source_path, criteria_path.parent))
    outputs = [args.report] + [f"{prefix}.{name}.png" for prefix in (args.heatmap, args.overlay)
                              if prefix for name, _ in regions]
    _check_output_paths(outputs, protected)
    output_regions = []
    for name, crop in regions:
        result = compare_region(ref, cand, crop)
        result["name"] = name
        output_regions.append(result)
        if args.heatmap or args.overlay:
            box = _crop_box(crop, ref.size)
            r, c = ref.convert("RGBA").crop(box), cand.convert("RGBA").crop(box)
            diff = ImageChops.difference(r, c).convert("RGB")
            for prefix, image in ((args.heatmap, ImageEnhance.Contrast(diff).enhance(4)),
                                  (args.overlay, Image.blend(r.convert("RGB"), c.convert("RGB"), .5))):
                if prefix:
                    buffer = BytesIO()
                    image.save(buffer, format="PNG")
                    _write_output(f"{prefix}.{name}.png", buffer.getvalue(), protected)
    report = {"schema_version": 1, "generated_utc": datetime.now(timezone.utc).isoformat(),
              "status": "not_evaluated", "equivalence": "not_evaluated",
              "reference": {"path": str(ref_path), "sha256": _sha256(ref_path), "state": args.reference_state},
              "candidate": {"path": str(cand_path), "sha256": _sha256(cand_path), "state": args.candidate_state},
              "tool_versions": {"python": platform.python_version(), "pillow": PILLOW_VERSION},
              "capture_metadata": {"reference_label": args.reference_label, "candidate_label": args.candidate_label},
              "regions": output_regions}
    if args.criteria:
        # Explicit criteria may evaluate named region metrics, never a hidden global aggregate.
        for region in output_regions:
            region["evaluation"] = evaluate_criteria(region["color"], criteria, Path(args.criteria).resolve().parent)
        evaluations = [r["evaluation"] for r in output_regions]
        report["status"] = "measured"
        report["equivalence"] = "pass" if all(e["equivalence"] == "pass" for e in evaluations) else "fail"
    _write_output(args.report, (json.dumps(report, indent=2, sort_keys=True) + "\n").encode(), protected)
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
                               ("status", "quality_gaps", "equality_eligible", "environment")}
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
    event_associations = {}
    if validation is not None:
        for case in validation["normalized_manifest"]["cases"]:
            events = []
            for record in case["measurements"]:
                if isinstance(record, dict) and record.get("name") == "input_events":
                    value = record.get("value")
                    if not isinstance(value, list) or any(not isinstance(event, dict) for event in value):
                        raise ValueError("input_events must be a list of event objects")
                    events.extend(value)
            if events:
                event_associations[case["id"]] = associate_timeline_events(report["timelines"][0], events)
    report["event_associations"] = event_associations
    protected = [path, *[_resolve_artifact(frame["file"], path.parent) for frame in frames]]
    if validation is not None:
        normalized = validation["normalized_manifest"]
        artifacts = list(normalized.get("artifacts", []))
        for case in normalized["cases"]:
            artifacts.extend(case["artifacts"])
        protected.extend(_resolve_artifact(item, path.parent) for item in artifacts)
    _write_output(args.report, (json.dumps(report, indent=2, sort_keys=True) + "\n").encode(), protected)
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
