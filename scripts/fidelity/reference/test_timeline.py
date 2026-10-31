import sys
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).parent))
from timeline import change_curve, frame_intervals_ms, summarize_intervals


class TimelineTests(unittest.TestCase):
    def test_actual_intervals_never_assume_target_fps(self):
        self.assertEqual(frame_intervals_ms([0, 33, 70]), [33, 37])
        self.assertEqual(summarize_intervals([33, 37]), {"count": 2, "min_ms": 33, "median_ms": 35.0, "max_ms": 37, "fps_median": 28.571})

    def test_change_curve_detects_only_observed_roi_changes(self):
        self.assertEqual(change_curve([[0, 0], [0, 0], [3, 4], [3, 4]], threshold=1), [0.0, 0.0, 5.0, 0.0])


if __name__ == '__main__':
    unittest.main()
