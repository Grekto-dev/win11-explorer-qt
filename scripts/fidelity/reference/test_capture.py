import sys
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).parent))
from capture import abs_value, click_events, move_events


class InputEncodingTests(unittest.TestCase):
    def test_abs_scale_endpoints_and_middle(self):
        self.assertEqual(abs_value(0, 1440), 0)
        self.assertEqual(abs_value(1439, 1440), 32767)
        self.assertEqual(abs_value(720, 1440), 16395)

    def test_move_and_click_event_order(self):
        self.assertEqual([e['type'] for e in move_events(100, 200, 1440, 900)], ['abs', 'abs'])
        self.assertEqual([e['data']['down'] for e in click_events()], [True, False])


if __name__ == '__main__':
    unittest.main()
