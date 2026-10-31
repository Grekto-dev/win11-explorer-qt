#!/usr/bin/env python3
"""Summarize guest-captured PNGs without inventing/interpolating frames."""
from __future__ import annotations
import json
import statistics
from pathlib import Path
from PIL import Image, ImageStat
from timeline import frame_intervals_ms, summarize_intervals, change_curve


def analyze(timeline_path: Path, frames_dir: Path, roi_local: tuple[int, int, int, int]) -> dict:
    timeline = json.loads(timeline_path.read_text(encoding='utf-8-sig'))
    frames = timeline['frames']
    timestamps = [float(f['timestamp_ms']) for f in frames]
    intervals = frame_intervals_ms(timestamps)
    means = []
    for frame in frames:
        with Image.open(frames_dir / frame['file']) as im:
            means.append(round(ImageStat.Stat(im.convert('L').crop(roi_local)).mean[0], 4))
    curve = change_curve(means)
    return {
        'frame_count': len(frames), 'duration_ms': timeline['duration_ms'],
        'intervals_ms': summarize_intervals(intervals),
        'roi_local_xyxy': list(roi_local), 'roi_mean_luma': means,
        'roi_frame_delta_luma': curve,
        'change_frames': [{'file': frames[i]['file'], 'timestamp_ms': timestamps[i], 'delta_luma': curve[i]}
                          for i in range(1, len(curve)) if curve[i] > 0],
        'events': timeline['events'],
    }

if __name__ == '__main__':
    import argparse
    p = argparse.ArgumentParser()
    p.add_argument('timeline', type=Path); p.add_argument('frames_dir', type=Path)
    p.add_argument('--roi', nargs=4, type=int, default=(325, 0, 520, 380))
    a = p.parse_args()
    print(json.dumps(analyze(a.timeline, a.frames_dir, tuple(a.roi)), indent=2))
