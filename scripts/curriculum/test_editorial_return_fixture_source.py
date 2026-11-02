"""Pinned Windows return archives stay independent of the integrated live asset."""
import hashlib
from pathlib import Path
import tempfile
import unittest
from unittest.mock import patch

import editorial_return_fixture_source as fixtures


class EditorialReturnFixtureSourceTests(unittest.TestCase):
    def setUp(self):
        self.live = fixtures.ROOT / fixtures.RELATIVE
        self.current = self.live.read_bytes()

    def tearDown(self):
        self.assertEqual(self.current, self.live.read_bytes())

    def test_exports_both_exact_publications_and_keeps_existing_bytes_and_mtimes(self):
        with tempfile.TemporaryDirectory(prefix="lgaed-editorial-return-") as directory:
            output = Path(directory) / "history"
            original = fixtures.export(output)
            intermediate = output / "lesson_estado_asignacion_y_copia-df2f9fa.json"
            self.assertEqual("1f88d26abb2b93108257f62a6e7914744bae8fe874f6664aac2fb7ad31ba8dcb",
                             hashlib.sha256(original.read_bytes()).hexdigest())
            self.assertEqual("7c3e963deba83299f771924ec2c0d1d06ef1eae3db4763bdffe2457aa47f2c81",
                             hashlib.sha256(intermediate.read_bytes()).hexdigest())
            self.assertNotEqual(original.read_bytes(), intermediate.read_bytes())
            self.assertNotEqual(intermediate.read_bytes(), self.current)
            before = {path: (path.read_bytes(), path.stat().st_mtime_ns) for path in (original, intermediate)}
            self.assertEqual(original, fixtures.export(output))
            self.assertEqual(before, {path: (path.read_bytes(), path.stat().st_mtime_ns) for path in before})

    def test_wrong_historical_source_fails_before_either_new_output_is_written(self):
        with tempfile.TemporaryDirectory(prefix="lgaed-editorial-return-") as directory:
            original = fixtures.export(Path(directory) / "valid").read_bytes()
            for wrong in (original, self.current, b"{\"wrong\":true}"):
                with self.subTest(source_sha256=hashlib.sha256(wrong).hexdigest()):
                    output = Path(directory) / hashlib.sha256(wrong).hexdigest()
                    with patch.object(fixtures.subprocess, "check_output", side_effect=[original, wrong]):
                        with self.assertRaises(ValueError):
                            fixtures.export(output)
                    self.assertFalse(output.exists())

    def test_existing_corrupt_intermediate_is_preserved_and_rejected(self):
        with tempfile.TemporaryDirectory(prefix="lgaed-editorial-return-") as directory:
            output = Path(directory) / "history"
            original = fixtures.export(output)
            intermediate = output / fixtures.INTERMEDIATE_NAME
            original_bytes = original.read_bytes()
            changed = intermediate.read_bytes() + b" "
            intermediate.write_bytes(changed)
            with self.assertRaises(FileExistsError):
                fixtures.export(output)
            self.assertEqual(changed, intermediate.read_bytes())
            self.assertEqual(original_bytes, original.read_bytes())


if __name__ == "__main__":
    unittest.main()
