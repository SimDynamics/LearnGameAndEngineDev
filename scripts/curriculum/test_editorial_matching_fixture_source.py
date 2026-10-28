"""Focused preservation checks for the immutable historical U9 QA fixture."""
from contextlib import contextmanager
import hashlib
import json
from pathlib import Path
import subprocess
import tempfile
import unittest
from unittest.mock import patch
import uuid

import editorial_matching_fixture_source as fixture


class EditorialMatchingFixtureSourceTest(unittest.TestCase):
    def setUp(self):
        fixture.historical_bytes.cache_clear()

    def tearDown(self):
        fixture.historical_bytes.cache_clear()

    @contextmanager
    def own_output(self):
        qa = (fixture.ROOT / "out/qa").resolve()
        qa.mkdir(parents=True, exist_ok=True)
        temporary = tempfile.TemporaryDirectory(prefix="editorial-matching-fixture-test-", dir=qa)
        output = Path(temporary.name).resolve()
        # Verify the absolute cleanup target before TemporaryDirectory removes
        # its own files; nothing outside this dedicated QA subtree is touched.
        self.assertTrue(output.is_relative_to(qa))
        self.assertNotEqual(output, qa)
        try:
            yield output
        finally:
            temporary.cleanup()

    def test_real_local_historical_bytes_and_original_matching_contract(self):
        payload = fixture.historical_bytes()  # Real local Git source, never mocked.
        self.assertEqual(103367, len(payload))
        self.assertEqual(
            "caf6601b9eb17bc71fa067bc334f0e20196ebe2604848fd40dc455cbeef5d836",
            hashlib.sha256(payload).hexdigest(),
        )
        document = json.loads(payload)
        block = document["pages"][7]["blocks"][1]
        response = block["responses"][0]
        self.assertEqual("contratos", response["id"])
        self.assertEqual("construccion", response["sources"][0]["id"])
        self.assertEqual("Construcción que termina normalmente", response["sources"][0]["label"])
        self.assertEqual("objeto_valido", response["validator"]["expected"]["construccion"])
        self.assertEqual(payload, fixture.verify_bytes(payload))

    def test_altered_byte_is_rejected(self):
        changed = bytearray(fixture.historical_bytes())
        changed[-1] ^= 1
        with self.assertRaisesRegex(RuntimeError, "SHA-256 mismatch"):
            fixture.verify_bytes(bytes(changed))

    def test_missing_local_object_never_fetches_or_substitutes_current_assets(self):
        reference = f"{fixture.BASELINE_COMMIT}:{fixture.RELATIVE_SOURCE}"
        command = ["git", "show", reference]
        with patch.object(fixture.subprocess, "check_output",
                side_effect=subprocess.CalledProcessError(128, command)) as show, \
                patch.object(fixture.subprocess, "run",
                    side_effect=AssertionError("No fetch or secondary command is permitted")) as other:
            with self.assertRaisesRegex(RuntimeError, "Missing local matching fixture"):
                fixture.historical_bytes()
        show.assert_called_once_with(command, cwd=fixture.ROOT, stderr=subprocess.PIPE)
        other.assert_not_called()

    def test_export_uses_only_own_qa_directory_and_is_idempotent(self):
        expected = fixture.historical_bytes()  # Real bytes also drive export.
        with self.own_output() as temporary:
            output = temporary / "u9-cbd9a38"
            target = fixture.export_fixture(output)
            self.assertEqual(output / fixture.FILENAME, target)
            self.assertTrue(target.resolve().is_relative_to(temporary))
            self.assertEqual(expected, target.read_bytes())
            before = (target.stat().st_mtime_ns, target.read_bytes())
            self.assertEqual(target, fixture.export_fixture(output))
            self.assertEqual(before, (target.stat().st_mtime_ns, target.read_bytes()))
            self.assertEqual([target], list(output.iterdir()))
            self.assertEqual([output], list(temporary.iterdir()))

    def test_live_assets_and_children_are_rejected_before_any_write(self):
        assets = (fixture.ROOT / "app/src/main/assets").resolve()
        child = assets / f"qa-forbidden-editorial-matching-{uuid.uuid4().hex}"
        self.assertFalse(child.exists())
        for output in (assets, child):
            with self.subTest(output=output), \
                    patch.object(Path, "mkdir", side_effect=AssertionError("Live assets must not be created")) as mkdir, \
                    patch.object(Path, "write_bytes", side_effect=AssertionError("Live assets must not be written")) as write:
                with self.assertRaisesRegex(ValueError, "cannot overwrite live curricular assets"):
                    fixture.export_fixture(output)
                mkdir.assert_not_called()
                write.assert_not_called()
        self.assertFalse(child.exists())


if __name__ == "__main__":
    unittest.main(verbosity=2)
