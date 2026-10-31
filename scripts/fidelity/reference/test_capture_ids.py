import sys
import tempfile
import unittest
from pathlib import Path
from unittest.mock import patch

sys.path.insert(0, str(Path(__file__).parent))
from capture import record, validate_action_id


class ActionIdTests(unittest.TestCase):
    def test_rejects_traversal_and_path_separators(self):
        for value in ('../escape', '..\\escape', 'a/b', 'a\\b'):
            with self.subTest(value=value), self.assertRaises(ValueError):
                validate_action_id(value)

    def test_invalid_id_fails_before_capture_or_output_creation(self):
        with tempfile.TemporaryDirectory() as tmp:
            out = Path(tmp) / 'out'
            with patch('capture.screenshot') as shot, patch('capture.send') as send:
                with self.assertRaises(ValueError):
                    record(out, [{'id': '../unsafe', 'kind': 'wait', 'seconds': 0}])
            shot.assert_not_called()
            send.assert_not_called()
            self.assertFalse(out.exists())

    def test_rejects_invalid_token_without_normalizing(self):
        with self.assertRaises(ValueError):
            validate_action_id(' spaces ')
        self.assertEqual(validate_action_id('menu-open_01'), 'menu-open_01')


if __name__ == '__main__':
    unittest.main()
