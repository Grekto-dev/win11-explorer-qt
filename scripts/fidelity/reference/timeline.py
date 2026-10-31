"""Helpers for reporting measured frame cadence and ROI intensity timelines."""
from __future__ import annotations

import statistics


def frame_intervals_ms(timestamps_ms: list[float]) -> list[float]:
    if any(b <= a for a, b in zip(timestamps_ms, timestamps_ms[1:])):
        raise ValueError("timestamps must be strictly increasing")
    return [round(b - a, 3) for a, b in zip(timestamps_ms, timestamps_ms[1:])]


def summarize_intervals(intervals: list[float]) -> dict:
    if not intervals:
        return {"count": 0, "min_ms": None, "median_ms": None, "max_ms": None, "fps_median": None}
    median = statistics.median(intervals)
    return {"count": len(intervals), "min_ms": min(intervals), "median_ms": round(median, 3),
            "max_ms": max(intervals), "fps_median": round(1000 / median, 3)}


def change_curve(values: list[float], threshold: float = 0) -> list[float]:
    """Absolute frame-to-frame ROI metric deltas; no temporal interpolation."""
    if threshold < 0:
        raise ValueError("threshold must be nonnegative")
    curve = [0.0]
    for a, b in zip(values, values[1:]):
        if isinstance(a, (list, tuple)) and isinstance(b, (list, tuple)):
            if len(a) != len(b):
                raise ValueError("curve vectors must have equal lengths")
            delta = sum((float(y) - float(x)) ** 2 for x, y in zip(a, b)) ** 0.5
        else:
            delta = abs(float(b) - float(a))
        curve.append(round(delta, 4) if delta >= threshold else 0.0)
    return curve
