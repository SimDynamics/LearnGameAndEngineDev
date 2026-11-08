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


# Zero-based compiler and complete battery-reference pages in the publications.
READER_PAGES = {"u11l19": (4, 7), "u11l20": (5, 8), "u11l21": (5, 7)}


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

    def test_u11_readers_keep_the_same_reference_starter_and_execution_oracle(self):
        byte_contracts = {
            "u11l19": (
                (11892, "c7827513af43c8e0892911f58da09e6bdd49a692289aaf543c36617fbbdf4f04"),
                (195, "3d2b056528db7e32e2394551560700d7674667bac61da8a2e432816502224a8d"),
                (650, "ca98b8a44f6f99f914dd9b48361087495e3e9c6dee9880a4a8c790093ef6692b"),
            ),
            "u11l20": (
                (12442, "467c96fc397ab887fb542cd36ba9a9cc77c8039fa7e0ba087c2ed547ce28cbea"),
                (206, "4d8beb91c5e3d51d5db268745d59b4f42e70041d71509b3387f5a49301a04aeb"),
                (711, "12cbf96e5dee77be943ba423ec1ea936e39997284352711783a1f707692418f1"),
            ),
            "u11l21": (
                (12921, "aec59ea4646f490597dc4e58b4af90b700dda37963b3bc514d56ba77e668fe94"),
                (218, "51a215219002d61b592fc70d75374335ae29e5ebb51a7720881f5cfbe6dfa06f"),
                (809, "2def5180eeae40e1781aa77fb0ac56801fe9c377cec6b642100e7c7a1e70f7ae"),
            ),
        }
        for name, (page, reference_page) in READER_PAGES.items():
            with self.subTest(lesson=name):
                before, after = (self.documents[name, side] for side in fixture.COMMITS)
                current = layout._blocks(after)
                old_editor = before["pages"][page]["blocks"][2]
                editor = current[f"block_{page:04d}_0002"]
                old_reference = before["pages"][reference_page]["blocks"][1]
                reference = current[f"block_{reference_page:04d}_0001"]
                self.assertEqual(old_editor, editor)
                self.assertEqual(old_reference, reference)
                self.assertEqual("code", reference["type"])
                self.assertEqual("cpp", reference["language"])
                self.assertEqual(1, len(editor["outputCheck"]["cases"]))
                case = editor["outputCheck"]["cases"][0]
                self.assertEqual("", case["standardInput"])
                self.assertEqual(1, len(case["outputs"]))
                # Authored source/expected-output bytes are not an execution
                # receipt or evidence that the Windows reader played anything.
                values = (reference["code"], editor["initialCode"], case["outputs"][0])
                for value, (size, digest) in zip(values, byte_contracts[name], strict=True):
                    encoded = value.encode("utf-8")
                    self.assertEqual(size, len(encoded))
                    self.assertEqual(digest, hashlib.sha256(encoded).hexdigest())
                # The revised guidance belongs to the page, not to the oracle.
                # These cases still use the compiler's generic verdict.
                self.assertNotIn("explanation", case)
                self.assertNotIn("reviewFeedback", case)

    def test_u11_readers_reorder_original_guidance_before_their_compiler(self):
        paragraph_chains = {
            "u11l19": (
                ("12c57e25c4552fc50fd218cad3bbbdf5293b286eaed8f809eeadac23868ebf8e",
                 "ccc7c1bbacf269ed0ca0ec8ccdf5fa311645d98a8da9ad2e2a485c034edcb130",
                 "11e228e3d636a0a940d85ffe068e3cbfb39c38fe7cb4321661d71f2198576664"),
                ("e7aabc9464c589e774b7c33dab163ddba3e99d264531a145f058f821015fbd37",
                 "620a0b8f5b1eaebfa74b6af1e9fd0e26c4738e90669a4894c95c3a09bb22bfa2"),
                ("958ced4d076334214d50b83ae9037cad847c63adf505c8cf6536ba09f7d2ec8f",
                 "5a65ce5f67b9810f74fc4d9d6b1f9978b6d68b354f854af58facad98bda3b20a"),
            ),
            "u11l20": (
                ("a1688e271a704709a8d0d44c718727d88d5eea377c7d7fef469ef3de5e3d8db6",
                 "f5c3f3e47de2b6142db7aa981e369840ee3f30ee4b85fdfff9231b84cf0fe90b"),
                ("f0adf115232510b37d472d576dc88da5365c116dcd0442ce33804b674d0ea93f",
                 "be09d909813e9ab682c380e93d17de3f8d8699bb02cf2ec1ef104f53770249ae"),
                ("933dade08349b80446ed7cad157f0265c082edac8f8cbba9c886788f5eeccdfd",
                 "52fac420e4399a7bcd572db4e2d55a35b351206abf90ee40bc735d0bb388e2d2"),
            ),
            "u11l21": (
                ("ec1da1e4ab8fc6afbe539e41a6933385d21cfbcf0c5141035484c3c8eb810741",
                 "c6c81086442afa502550bb06ff98e7b25303ffa85d5d865585db935bd2932455"),
                ("6b4b5fda01b16f90576589a4078b992ac0decd104cc57ac58dbc2be228101773",
                 "590b95539de317ec0e1ac201851b599f46d782e4453b175b2f8a81c978c91158"),
                ("79d1aa206257b8dfdbab019177acba1d2ce2982451349f3d18f89d920210f4f0",
                 "bfd4bd6f4dd5cd73eb3b09dfce9eb31eeabe61d69beed22354d300bbf0a0fa62"),
            ),
        }
        for name, (page_index, _) in READER_PAGES.items():
            with self.subTest(lesson=name):
                before, after = (self.documents[name, side] for side in fixture.COMMITS)
                original = layout._blocks(layout.prepare(before))
                current = layout._blocks(after)
                page = after["stateLayout"]["current"][page_index]
                identities = [f"block_{page_index:04d}_{index:04d}" for index in (0, 1, 3, 4, 2)]
                self.assertEqual(f"page_{page_index:04d}", page["id"])
                self.assertEqual(identities, page["blocks"])
                self.assertEqual(set(identities), {item["id"] for item in after["stateLayout"]["origin"][page_index]["blocks"]})
                self.assertFalse(set(identities) & {item["id"] for item in after["stateLayout"]["additions"]})
                positions = layout.validate(after)
                for visible, historical in enumerate((0, 1, 3, 4, 2)):
                    self.assertEqual((page_index, historical), positions[page_index, visible])
                title = f"block_{page_index:04d}_0000"
                self.assertEqual(original[title], current[title])
                # The unchanged title and replaced instruction are distinct
                # pause sources. All three prose blocks retain their owners;
                # their fingerprints authenticate text AND original emphasis.
                for index, chain in zip((1, 3, 4), paragraph_chains[name], strict=True):
                    identity = f"block_{page_index:04d}_{index:04d}"
                    with self.subTest(owner=identity):
                        self.assertEqual("paragraph", original[identity]["type"])
                        self.assertEqual("paragraph", current[identity]["type"])
                        self.assertNotEqual(original[identity]["text"], current[identity]["text"])
                        emphasis = ([{"text": "Compilar, ejecutar y comprobar", "style": "bold"}]
                                    if index == 1 else [])
                        self.assertEqual(emphasis, original[identity].get("emphasis", []))
                        self.assertEqual(chain[0], layout.fingerprint(original[identity]))
                        self.assertEqual(chain[-1], layout.fingerprint(current[identity]))
                        # L19's instruction was revised twice; it must retain
                        # its intermediate edge, not invent a direct bridge.
                        self.assertEqual(
                            [{"id": identity, "beforeSha256": old, "afterSha256": new}
                             for old, new in zip(chain, chain[1:])],
                            [edge for edge in after["stateLayout"]["editorial"] if edge["id"] == identity],
                        )

    def test_u11_readers_keep_prior_ordinary_response_contracts(self):
        prerequisites = {
            "u11l19": ((0, 3, "multiple_choice"), (1, 3, "multiple_choice"),
                       (3, 2, "order_code"), (3, 3, "output_prediction"), (3, 4, "multiple_choice")),
            "u11l20": ((0, 3, "multiple_choice"), (1, 3, "multiple_choice"), (2, 3, "order_code"),
                       (4, 1, "output_prediction"), (4, 2, "multiple_choice"), (4, 3, "multiple_choice")),
            "u11l21": ((0, 3, "multiple_choice"), (1, 3, "multiple_choice"), (2, 2, "order_code"),
                       (4, 1, "output_prediction"), (4, 2, "multiple_choice"), (4, 3, "multiple_choice")),
        }
        for name, (compiler_page, _) in READER_PAGES.items():
            with self.subTest(lesson=name):
                before, after = (self.documents[name, side] for side in fixture.COMMITS)
                current = layout._blocks(after)
                expected = prerequisites[name]
                for side, document in (("before", before), ("after", after)):
                    actual = tuple((p, b, block["type"])
                                   for p, page in enumerate(document["pages"][:compiler_page])
                                   for b, block in enumerate(page["blocks"])
                                   if block["type"] not in ("heading", "paragraph", "code", "note"))
                    self.assertEqual(expected, actual, side)
                for page, block_index, _ in expected:
                    identity = f"block_{page:04d}_{block_index:04d}"
                    self.assertEqual(identity, after["stateLayout"]["current"][page]["blocks"][block_index])
                    old = before["pages"][page]["blocks"][block_index]
                    # L20 legitimately revises its questions/explanations.
                    # Options, ordering, code, answers and authored IDs remain
                    # identical, so the synthetic QA prerequisite responses
                    # can survive migration without inventing compiler credit.
                    def response_contract(value):
                        return {key: item for key, item in value.items()
                                if key not in ("question", "explanation", "reviewFeedback")}
                    self.assertEqual(response_contract(old), response_contract(current[identity]))

    def test_u11_readers_keep_followup_predictions_separate_from_the_editor(self):
        for name, prediction, page_index in (
            ("u11l19", "work_u11l19_nombre_y_igual", 5),
            ("u11l21", "work_u11l21_nombres_completos", 6),
        ):
            with self.subTest(lesson=name):
                before, after = (self.documents[name, side] for side in fixture.COMMITS)
                original = layout._blocks(layout.prepare(before))
                current = layout._blocks(after)
                positions = layout.validate(after)
                self.assertNotIn(prediction, original)
                self.assertEqual("output_prediction", current[prediction]["type"])
                self.assertEqual(prediction, after["stateLayout"]["current"][page_index]["blocks"][1])
                addition = next(item for item in after["stateLayout"]["additions"] if item["id"] == prediction)
                self.assertEqual({"page": 8, "block": 0}, addition["storage"])
                self.assertEqual(f"page_{page_index:04d}", addition["progressPage"])
                self.assertEqual((8, 0), positions[page_index, 1])
                self.assertEqual((page_index, 1), positions[page_index, 2])
                self.assertEqual(before["pages"][page_index]["blocks"][1], after["pages"][page_index]["blocks"][2])
        # L20 instead retains its original later prediction and note. Its
        # revised note is not a newly added activity or part of the compiler.
        before, after = (self.documents["u11l20", side] for side in fixture.COMMITS)
        self.assertEqual([], after["stateLayout"]["additions"])
        self.assertEqual(["block_0007_0000", "block_0007_0001", "block_0007_0002"],
                         after["stateLayout"]["current"][7]["blocks"])
        old, current = before["pages"][7]["blocks"], after["pages"][7]["blocks"]
        self.assertEqual("output_prediction", current[1]["type"])
        for field in ("id", "code", "language", "answers"):
            self.assertEqual(old[1][field], current[1][field])
        self.assertEqual("note", old[2]["type"])
        self.assertEqual("note", current[2]["type"])
        self.assertNotEqual(old[2]["text"], current[2]["text"])

    def test_u11_reader_exports_require_exact_lf_publication_bytes(self):
        # Historical-reader inputs come from the exporter's exact Git bytes.
        # They are distinct from current assets checked out with CRLF. The
        # existing all-sources test separately rejects the opposite revision.
        for name in READER_PAGES:
            for side in fixture.COMMITS:
                raw = self.raw[name, side]
                self.assertNotIn(b"\r", raw)
                self.assertEqual(raw, fixture.verify_bytes(name, side, raw))
                for ending, changed in (
                    ("CRLF", raw.replace(b"\n", b"\r\n")),
                    ("mixed", raw.replace(b"\n", b"\r\n", 1)),
                ):
                    with self.subTest(lesson=name, side=side, ending=ending):
                        self.assertNotEqual(raw, changed)
                        with self.assertRaisesRegex(ValueError, "pinned Git bytes"):
                            fixture.verify_bytes(name, side, changed)

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
