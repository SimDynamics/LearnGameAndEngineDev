"""Pinned transport, complete fixture manifest and protected export tests.

These tests do not claim native C++ execution or edit any published resource.
The Windows harness grades the attempts through the actual engine and worker.
"""
from __future__ import annotations

from collections import Counter
from copy import deepcopy
import hashlib
import json
from pathlib import Path, PurePosixPath
import subprocess
import tempfile
import unittest
from unittest.mock import patch

import edition_source_fixture_source as fixture


class EditionSourceFixtureTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.payloads = fixture.export_payloads()
        cls.manifest = json.loads(cls.payloads["manifest.json"])

    def test_all_publications_are_pinned_and_exported_without_reserialization(self):
        self.assertEqual(18, len(fixture.PUBLICATIONS))
        self.assertEqual(5, len(fixture.INDEXES))
        self.assertEqual(43, len(self.payloads))
        for entry in self.manifest["lessons"]:
            with self.subTest(lesson=entry["id"]):
                raw = fixture.published_bytes(entry["id"])
                spec = fixture.PUBLICATIONS[entry["id"]]
                self.assertEqual(raw, self.payloads[entry["original"]])
                self.assertEqual(spec["sha256"], hashlib.sha256(raw).hexdigest())
                blob = hashlib.sha1(b"blob " + str(len(raw)).encode() + b"\0" + raw).hexdigest()
                self.assertEqual(spec["blobSha"], blob)
                self.assertEqual(blob, entry["publishedBlobSha"])
                self.assertEqual(spec["path"], entry["publishedPath"])
                original = json.loads(raw)
                current = json.loads(self.payloads[entry["edition"]])
                self.assertNotEqual(original["id"], current["id"])
                self.assertEqual(original["stateLayout"]["current"], current["stateLayout"]["current"])
                self.assertEqual(original, current["courseEditionSource"]["document"])
                self.assertEqual([], current["stateLayout"]["additions"])
                self.assertEqual([], current["stateLayout"]["editorial"])
                self.assertEqual({"es"}, set(current["stateLayout"]["family"]))
                self.assertFalse(any(key in current["stateLayout"] for key in (
                    "sourceFormatRevisions", "compilerRevisions", "choiceRevisions",
                    "retired", "retirementHistory")))
                for key, value in original.items():
                    if key not in {"id", "stateLayout"}:
                        self.assertEqual(value, current[key], key)
                fingerprint = fixture.state.fingerprint
                for page, origin in zip(current["pages"], current["stateLayout"]["origin"], strict=True):
                    self.assertEqual([fingerprint(block) for block in page["blocks"]],
                                     [block["sha256"] for block in origin["blocks"]])

    def test_manifest_pins_every_path_and_the_exact_41_block_inventory(self):
        files = self.manifest["files"]
        self.assertEqual(set(self.payloads) - {"manifest.json"}, set(files))
        for name, digest in files.items():
            with self.subTest(file=name):
                self.assertEqual(digest, hashlib.sha256(self.payloads[name]).hexdigest())
                path = PurePosixPath(name)
                self.assertFalse(path.is_absolute())
                self.assertNotIn("..", path.parts)
                self.assertNotIn("\\", name)
        inventory = [block for lesson in self.manifest["lessons"] for block in lesson["blocks"]]
        self.assertEqual(fixture.EXPECTED_KINDS, Counter(block["kind"] for block in inventory))
        self.assertEqual(41, len(inventory))
        self.assertEqual(36, sum(block["kind"] != "reference" for block in inventory))
        self.assertEqual(52, sum(len(block.get("attempts", [])) for block in inventory))
        for lesson in self.manifest["lessons"]:
            original = json.loads(self.payloads[lesson["original"]])
            blocks = fixture._blocks(original)
            for row in lesson["blocks"]:
                self.assertEqual(fixture.state.fingerprint(blocks[row["owner"]]), row["blockFingerprint"])
                if row["kind"] == "reference":
                    self.assertEqual(blocks[row["owner"]]["code"], row["source"])
                    self.assertEqual(fixture.sha256(row["source"]), row["sourceSha256"])

    def test_cpp_attempts_keep_full_sources_first_inputs_and_declared_derivations(self):
        mutations = Counter()
        projects = 0
        for lesson in self.manifest["lessons"]:
            blocks = fixture._blocks(json.loads(self.payloads[lesson["original"]]))
            for row in lesson["blocks"]:
                if "attempts" not in row:
                    continue
                self.assertEqual([True, False], [attempt["accepted"] for attempt in row["attempts"]])
                owner = blocks[row["owner"]]
                expected_input = owner.get("outputCheck", {}).get("cases", [{}])[0].get("standardInput", "")
                for attempt in row["attempts"]:
                    with self.subTest(lesson=lesson["id"], owner=row["owner"], attempt=attempt["label"]):
                        self.assertIs(type(attempt["accepted"]), bool)
                        self.assertEqual(expected_input, attempt["standardInput"])
                        self.assertEqual(fixture.sha256(attempt["source"]), attempt["sourceSha256"])
                        provenance = attempt["provenance"]
                        authentic = blocks[provenance["owner"]][provenance["field"]]
                        self.assertEqual(fixture.sha256(authentic), provenance["sourceSha256"])
                        if "mutation" not in provenance:
                            self.assertEqual(authentic, attempt["source"])
                        else:
                            mutation = provenance["mutation"]
                            mutations[mutation["kind"]] += 1
                            self.assertNotEqual(authentic, attempt["source"])
                            if mutation["kind"] == "insert-diagnostic-statement":
                                statement = mutation["statement"]
                                self.assertIn(statement + "\n", attempt["source"])
                                self.assertNotIn(statement, authentic)
                                self.assertEqual(authentic, attempt["source"].replace("    " + statement + "\n", "", 1))
                            elif mutation["kind"] == "normal-exit-wrapper":
                                self.assertIn("int edition_source_original_main()", attempt["source"])
                                self.assertTrue(attempt["source"].endswith(
                                    "int main() { edition_source_original_main(); return 0; }\n"))
                            else:
                                self.fail("Unreviewed source derivation")
                        if "projectFiles" in attempt:
                            projects += 1
                            self.assertEqual(blocks[provenance["owner"]]["projectFiles"], attempt["projectFiles"])
                            self.assertEqual(blocks[provenance["owner"]]["sourceFiles"], attempt["sourceFiles"])
                            self.assertEqual([attempt["source"]], [
                                item["initialCode"] for item in attempt["projectFiles"] if item["path"] == "main.cpp"])
                            for item in attempt["projectFiles"]:
                                self.assertEqual({"path", "initialCode"}, set(item))
                                self.assertEqual(fixture.sha256(item["initialCode"]),
                                                 attempt["projectFileSha256"][item["path"]])
        self.assertEqual(24, projects)
        self.assertEqual({"insert-diagnostic-statement": 5, "normal-exit-wrapper": 2}, mutations)

    def test_matching_payloads_are_complete_and_wrong_pairs_remain_one_to_one(self):
        matching = []
        for lesson in self.manifest["lessons"]:
            blocks = fixture._blocks(json.loads(self.payloads[lesson["original"]]))
            for row in lesson["blocks"]:
                if row["kind"] != "matching":
                    continue
                matching.append((lesson["id"], row["owner"]))
                response = blocks[row["owner"]]["responses"][0]
                self.assertEqual({row["responseId"]}, set(row["correct"]))
                self.assertEqual({row["responseId"]}, set(row["incorrect"]))
                correct = row["correct"][row["responseId"]]
                incorrect = row["incorrect"][row["responseId"]]
                self.assertEqual("matching", correct["type"])
                self.assertEqual("matching", incorrect["type"])
                self.assertEqual(response["validator"]["expected"], correct["pairs"])
                self.assertEqual(set(correct["pairs"]), set(incorrect["pairs"]))
                self.assertEqual(set(correct["pairs"].values()), set(incorrect["pairs"].values()))
                self.assertEqual(2, sum(correct["pairs"][key] != value
                                        for key, value in incorrect["pairs"].items()))
        self.assertEqual([("u9l13", "block_0007_0001"), ("u11l15", "block_0008_0002")], matching)

    def test_isolated_catalog_is_one_real_descriptor_with_all_18_copies(self):
        catalog = self.manifest["catalog"]
        course = json.loads(self.payloads[catalog["course"]])
        units = {value["unitId"]: value for name in catalog["units"]
                 for value in [json.loads(self.payloads[name])]}
        lessons = {value["id"]: value for entry in self.manifest["lessons"]
                   for value in [json.loads(self.payloads[entry["edition"]])]}
        self.assertEqual(fixture.DESCRIPTOR, course["courseEdition"])
        self.assertEqual([1, 2, 3, 4], [unit["number"] for unit in course["units"]])
        self.assertEqual([3, 4, 8, 3], [len(unit["lessons"]) for unit in units.values()])
        self.assertEqual(18, len(lessons))
        fixture.course_edition.validate_active_course(course, units, lessons)

    def test_byte_and_git_blob_pins_both_reject_changed_publications(self):
        name = "u6l2"
        spec = fixture.PUBLICATIONS[name]
        raw = self.payloads["originals/u6l2.json"]
        with self.assertRaisesRegex(ValueError, "pinned publication"):
            fixture.verify_bytes(spec, raw + b"\n")
        forged = deepcopy(spec)
        forged["blobSha"] = "0" * 40
        with self.assertRaisesRegex(ValueError, "pinned publication"):
            fixture.verify_bytes(forged, raw)
        forged = deepcopy(spec)
        forged["sha256"] = "0" * 64
        with self.assertRaisesRegex(ValueError, "pinned publication"):
            fixture.verify_bytes(forged, raw)

    def test_missing_object_fails_without_a_live_asset_or_fetch_fallback(self):
        with patch.object(fixture.subprocess, "check_output",
                          side_effect=subprocess.CalledProcessError(128, ["git", "show"])) as command:
            with self.assertRaisesRegex(RuntimeError, "no fetch or current-asset fallback"):
                fixture.published_bytes("u6l2")
        self.assertEqual(1, command.call_count)
        args, options = command.call_args
        self.assertEqual("git", args[0][0])
        self.assertEqual("show", args[0][-2])
        self.assertEqual("1", options["env"]["GIT_NO_LAZY_FETCH"])
        self.assertEqual("1", options["env"]["GIT_NO_REPLACE_OBJECTS"])

    def test_protected_export_is_idempotent_and_preflights_the_last_conflict(self):
        with tempfile.TemporaryDirectory(prefix="edition-source-export-") as temporary:
            root = Path(temporary)
            output = root / "complete"
            with patch.object(fixture, "export_payloads", return_value=self.payloads):
                exported = fixture.export(output)
                self.assertEqual(43, len(exported))
                before = {name: ((output / name).read_bytes(), (output / name).stat().st_mtime_ns)
                          for name in self.payloads}
                fixture.export(output)
                self.assertEqual(before, {
                    name: ((output / name).read_bytes(), (output / name).stat().st_mtime_ns)
                    for name in self.payloads})
                conflict = root / "conflict"
                conflict.mkdir()
                (conflict / "manifest.json").write_bytes(b"foreign existing file")
                with self.assertRaises(FileExistsError):
                    fixture.export(conflict)
                self.assertEqual([conflict / "manifest.json"], list(conflict.iterdir()))
                self.assertEqual(b"foreign existing file", (conflict / "manifest.json").read_bytes())

    def test_export_rejects_authored_paths_and_symbolic_destinations(self):
        with patch.object(fixture, "export_payloads", return_value=self.payloads):
            with self.assertRaisesRegex(ValueError, "authored/source"):
                fixture.export(fixture.ROOT / "app/src/edition-source-forbidden-output")
            with tempfile.TemporaryDirectory(prefix="edition-source-alias-") as temporary:
                root = Path(temporary)
                target = root / "target"
                target.mkdir()
                alias = root / "alias"
                try:
                    alias.symlink_to(target, target_is_directory=True)
                except (OSError, NotImplementedError):
                    # Windows hosts may intentionally withhold symlink creation.
                    # The production guard is also exercised by existing native
                    # course-edition reparse-point tests.
                    return
                with self.assertRaisesRegex(ValueError, "symlinks|junctions|reparse"):
                    fixture.export(alias)
                self.assertEqual([], list(target.iterdir()))


if __name__ == "__main__":
    unittest.main()

