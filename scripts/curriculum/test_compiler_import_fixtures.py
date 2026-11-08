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

    def test_u11l21_reader_keeps_the_same_reference_starter_and_execution_oracle(self):
        before, after = (self.documents["u11l21", side] for side in fixture.COMMITS)
        current = layout._blocks(after)
        old_editor = before["pages"][5]["blocks"][2]
        editor = current["block_0005_0002"]
        old_reference = before["pages"][7]["blocks"][1]
        reference = current["block_0007_0001"]
        self.assertEqual(old_editor, editor)
        self.assertEqual(old_reference, reference)
        self.assertEqual("code", reference["type"])
        self.assertEqual("cpp", reference["language"])
        self.assertEqual(1, len(editor["outputCheck"]["cases"]))
        case = editor["outputCheck"]["cases"][0]
        self.assertEqual("", case["standardInput"])
        self.assertEqual(1, len(case["outputs"]))
        # These are authored source/expected-output bytes, not an execution
        # receipt or evidence that the Windows reader has played anything.
        for value, size, digest in (
            (reference["code"], 12921,
             "aec59ea4646f490597dc4e58b4af90b700dda37963b3bc514d56ba77e668fe94"),
            (editor["initialCode"], 218,
             "51a215219002d61b592fc70d75374335ae29e5ebb51a7720881f5cfbe6dfa06f"),
            (case["outputs"][0], 809,
             "2def5180eeae40e1781aa77fb0ac56801fe9c377cec6b642100e7c7a1e70f7ae"),
        ):
            encoded = value.encode("utf-8")
            self.assertEqual(size, len(encoded))
            self.assertEqual(digest, hashlib.sha256(encoded).hexdigest())
        # Neither publication authors per-case feedback. The changed
        # reading passage belongs to the page's paragraph, not to this oracle.
        self.assertNotIn("explanation", case)
        self.assertNotIn("reviewFeedback", case)

    def test_u11l21_reader_reorders_original_guidance_before_its_compiler(self):
        before, after = (self.documents["u11l21", side] for side in fixture.COMMITS)
        original = layout._blocks(layout.prepare(before))
        current = layout._blocks(after)
        page = after["stateLayout"]["current"][5]
        identities = [f"block_0005_{index:04d}" for index in (0, 1, 3, 4, 2)]
        self.assertEqual("page_0005", page["id"])
        self.assertEqual(identities, page["blocks"])
        self.assertEqual(set(identities), {item["id"] for item in after["stateLayout"]["origin"][5]["blocks"]})
        self.assertFalse(set(identities) & {item["id"] for item in after["stateLayout"]["additions"]})
        positions = layout.validate(after)
        for visible, historical in enumerate((0, 1, 3, 4, 2)):
            self.assertEqual((5, historical), positions[5, visible])
        self.assertEqual(original["block_0005_0000"], current["block_0005_0000"])
        # The identical title is the compatible-pause source for native QA.
        # The first old paragraph has a complete newly authored replacement;
        # formatting/criteria paragraphs also retain their original owners.
        for identity, before_hash, after_hash in (
            ("block_0005_0001",
             "ec1da1e4ab8fc6afbe539e41a6933385d21cfbcf0c5141035484c3c8eb810741",
             "c6c81086442afa502550bb06ff98e7b25303ffa85d5d865585db935bd2932455"),
            ("block_0005_0003",
             "6b4b5fda01b16f90576589a4078b992ac0decd104cc57ac58dbc2be228101773",
             "590b95539de317ec0e1ac201851b599f46d782e4453b175b2f8a81c978c91158"),
            ("block_0005_0004",
             "79d1aa206257b8dfdbab019177acba1d2ce2982451349f3d18f89d920210f4f0",
             "bfd4bd6f4dd5cd73eb3b09dfce9eb31eeabe61d69beed22354d300bbf0a0fa62"),
        ):
            with self.subTest(owner=identity):
                self.assertEqual("paragraph", original[identity]["type"])
                self.assertEqual("paragraph", current[identity]["type"])
                self.assertNotEqual(original[identity]["text"], current[identity]["text"])
                self.assertEqual(before_hash, layout.fingerprint(original[identity]))
                self.assertEqual(after_hash, layout.fingerprint(current[identity]))
                self.assertIn({"id": identity, "beforeSha256": before_hash, "afterSha256": after_hash},
                              after["stateLayout"]["editorial"])
        prediction = "work_u11l21_nombres_completos"
        self.assertNotIn(prediction, original)
        self.assertEqual("output_prediction", current[prediction]["type"])
        self.assertEqual(prediction, after["stateLayout"]["current"][6]["blocks"][1])
        addition = next(item for item in after["stateLayout"]["additions"] if item["id"] == prediction)
        self.assertEqual({"page": 8, "block": 0}, addition["storage"])
        self.assertEqual("page_0006", addition["progressPage"])
        self.assertEqual((8, 0), positions[6, 1])
        self.assertEqual((6, 1), positions[6, 2])
        self.assertEqual(before["pages"][6]["blocks"][1], after["pages"][6]["blocks"][2])

    def test_u11l21_reader_exports_require_exact_lf_publication_bytes(self):
        # Historical-reader inputs come from the exporter's exact Git bytes.
        # They are distinct from current assets checked out with CRLF. The
        # existing all-sources test separately rejects the opposite revision.
        for side in fixture.COMMITS:
            raw = self.raw["u11l21", side]
            self.assertNotIn(b"\r", raw)
            self.assertEqual(raw, fixture.verify_bytes("u11l21", side, raw))
            for ending, changed in (
                ("CRLF", raw.replace(b"\n", b"\r\n")),
                ("mixed", raw.replace(b"\n", b"\r\n", 1)),
            ):
                with self.subTest(side=side, ending=ending):
                    self.assertNotEqual(raw, changed)
                    with self.assertRaisesRegex(ValueError, "pinned Git bytes"):
                        fixture.verify_bytes("u11l21", side, changed)

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
