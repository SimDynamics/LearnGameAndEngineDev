"""Verify the genuine lesson history used by focal Windows compiler imports."""
from copy import deepcopy
import hashlib
import json
from pathlib import Path
import subprocess
import tempfile
import unittest
from unittest.mock import patch

import compiler_import_fixture_source as fixture
import stable_state_layout as layout


class CompilerImportFixtureTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.raw = {(name, side): fixture.published_bytes(name, side)
                   for name in fixture.FIXTURES for side in fixture.COMMITS}
        cls.documents = {key: json.loads(payload) for key, payload in cls.raw.items()}

    def test_five_actual_publications_preserve_seven_positional_compiler_owners(self):
        total = 0
        for name, spec in fixture.FIXTURES.items():
            with self.subTest(lesson=name):
                before, after = (self.documents[name, side] for side in fixture.COMMITS)
                fixture.validate_pair(name, before, after)
                self.assertNotIn("stateLayout", before)
                self.assertEqual(2, after["stateLayout"]["version"])
                self.assertTrue(after["compilerPuzzlesRequired"])
                self.assertEqual(layout.prepare(before)["stateLayout"]["origin"], after["stateLayout"]["origin"])
                for identity, (page, block) in spec["owners"].items():
                    self.assertEqual("cpp_compiler", before["pages"][page]["blocks"][block]["type"])
                    self.assertEqual(before["pages"][page]["blocks"][block]["initialCode"],
                                     layout._blocks(after)[identity]["initialCode"])
                    response_page, response_block = spec["responses"][identity]
                    original_response = before["pages"][response_page]["blocks"][response_block]
                    current_response = layout._blocks(after)[f"block_{response_page:04d}_{response_block:04d}"]
                    self.assertEqual("output_prediction", original_response["type"])
                    self.assertEqual(original_response["answers"], current_response["answers"])
                    total += 1
        self.assertEqual(7, total)

    def test_output_contracts_keep_each_original_case_and_exact_byte_alternative(self):
        for name, spec in fixture.FIXTURES.items():
            before, after = (self.documents[name, side] for side in fixture.COMMITS)
            for identity, (page, block) in spec["owners"].items():
                with self.subTest(lesson=name, owner=identity):
                    old = fixture.execution_payload(before["pages"][page]["blocks"][block])
                    current = fixture.execution_payload(layout._blocks(after)[identity])
                    self.assertEqual(1, len(old["outputCheck"]["cases"]))
                    if name == "u12l20" and identity == "block_0005_0004":
                        original = old["outputCheck"]["cases"][0]["outputs"][0]
                        outputs = current["outputCheck"]["cases"][0]["outputs"]
                        self.assertEqual([original, original.replace("\n", "\r\n")], outputs)
                        self.assertEqual((802, 42, 3), (len(original), original.count("\n"), original.count("\r")))
                        self.assertIn("< \t\n\r\f\v>", original)
                        self.assertEqual(844, len(outputs[1]))
                        current["outputCheck"]["cases"][0]["outputs"] = [original]
                    self.assertEqual(old, current)

    def test_all_ten_sources_are_pinned_raw_git_bytes(self):
        for (name, side), payload in self.raw.items():
            with self.subTest(lesson=name, side=side):
                self.assertEqual(fixture.FIXTURES[name][side], hashlib.sha256(payload).hexdigest())
                with patch.object(fixture.subprocess, "check_output", return_value=payload + b"\n"):
                    with self.assertRaisesRegex(ValueError, "pinned Git bytes"):
                        fixture.published_bytes(name, side)
                opposite = self.raw[name, "after" if side == "before" else "before"]
                with patch.object(fixture.subprocess, "check_output", return_value=opposite):
                    with self.assertRaisesRegex(ValueError, "pinned Git bytes"):
                        fixture.published_bytes(name, side)
        with self.assertRaises(ValueError):
            fixture.published_bytes("unknown", "before")

    def test_rejects_foreign_ownership_source_or_execution_requirements(self):
        for name, spec in fixture.FIXTURES.items():
            identity = next(iter(spec["owners"]))
            for mutation in ("family", "origin", "source", "input", "output", "required"):
                with self.subTest(lesson=name, mutation=mutation):
                    before = self.documents[name, "before"]
                    changed = deepcopy(self.documents[name, "after"])
                    owner = layout._blocks(changed)[identity]
                    if mutation == "family": changed["stateLayout"]["family"]["es"]["sha256"] = "0" * 64
                    elif mutation == "origin": changed["stateLayout"]["origin"][0]["blocks"][0] = "foreign"
                    elif mutation == "source": owner["initialCode"] += "\n// another program\n"
                    elif mutation == "input": owner["outputCheck"]["cases"][0]["standardInput"] = "different"
                    elif mutation == "output": owner["outputCheck"]["cases"][0]["outputs"][0] += "different"
                    else: changed["compilerPuzzlesRequired"] = False
                    with self.assertRaises((ValueError, layout.LayoutError)):
                        fixture.validate_pair(name, before, changed)

    def test_export_preserves_exact_publications_and_never_rewrites_same_files(self):
        with tempfile.TemporaryDirectory() as folder:
            output = Path(folder) / "published"
            manifest_path = fixture.export(output)
            existing = {p.name: (p.read_bytes(), p.stat().st_mtime_ns) for p in output.iterdir()}
            self.assertEqual(manifest_path, fixture.export(output))
            self.assertEqual(existing, {p.name: (p.read_bytes(), p.stat().st_mtime_ns) for p in output.iterdir()})
            manifest = json.loads(manifest_path.read_bytes())
            self.assertEqual("published-positional-compiler-import", manifest["kind"])
            self.assertEqual(0, manifest["coordinateBase"])
            self.assertEqual(11, len(existing))
            for name, spec in fixture.FIXTURES.items():
                record = manifest["lessons"][name]
                self.assertEqual({k: list(v) for k, v in spec["owners"].items()}, record["owners"])
                self.assertEqual({k: list(v) for k, v in spec["responses"].items()}, record["responses"])
                for side in fixture.COMMITS:
                    self.assertEqual(fixture.COMMITS[side], record[side]["commit"])
                    self.assertEqual(spec[side], record[side]["sha256"])
                    self.assertEqual(self.raw[name, side], (output / record[side]["file"]).read_bytes())

    def test_missing_history_or_existing_conflict_fails_before_creating_other_files(self):
        with tempfile.TemporaryDirectory() as folder:
            output = Path(folder) / "missing"
            with patch.object(fixture.subprocess, "check_output", side_effect=subprocess.CalledProcessError(128, "git show")):
                with self.assertRaises(subprocess.CalledProcessError):
                    fixture.export(output)
            self.assertFalse(output.exists())
            output = Path(folder) / "conflict"
            output.mkdir()
            retained = output / "u12l20-after.json"
            retained.write_bytes(b"original independent QA evidence\n")
            with self.assertRaises(FileExistsError):
                fixture.export(output)
            self.assertEqual([retained], list(output.iterdir()))
            self.assertEqual(b"original independent QA evidence\n", retained.read_bytes())
        with self.assertRaises(ValueError):
            fixture.export(fixture.ROOT / "native" / "compiler-import-output")


if __name__ == "__main__":
    unittest.main()
