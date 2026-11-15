"""Stable owner resolution for the real AssetCheck reference compiler.

These checks exercise extraction only. The existing MSVC/GCC workflow still
compiles and executes the unchanged curricular sources against their oracles.
"""
from copy import deepcopy
import json
from pathlib import Path
import unittest
from unittest.mock import patch

import stable_state_layout as state
import assetcheck_curricular_targets as targets
import verify_assetcheck_editor_references_cpp23 as verifier


def mapped_fixture():
    return state.prepare({
        "pages": [
            {"blocks": [{"type": "paragraph", "text": "Introducción"},
                        {"type": "paragraph", "text": "Introducción"},
                        {"type": "cpp_compiler", "initialCode": "int main() {}"}]},
            {"blocks": [{"type": "code", "language": "cpp", "code": "int value = 1;"},
                        {"type": "paragraph", "text": "Introducción"}]},
        ],
    })


class AssetCheckReferenceIdentityTests(unittest.TestCase):
    @staticmethod
    def extract(document, spec):
        with patch.object(Path, "read_text", return_value=json.dumps(document, ensure_ascii=False)):
            return verifier.lesson_reference(*spec)

    def test_declared_editors_keep_exact_starters_and_cases(self):
        for spec in verifier.LESSONS:
            with self.subTest(lesson=spec[0]):
                document = json.loads((verifier.CONTENT / spec[0]).read_text(encoding="utf-8"))
                before = deepcopy(document)
                owner = targets.EDITOR_OWNERS[spec[0]]
                positions = [(pi, bi) for pi, record in enumerate(document["stateLayout"]["current"])
                             for bi, ident in enumerate(record["blocks"]) if ident == owner]
                self.assertEqual(1, len(positions))
                pi, bi = positions[0]
                editor = document["pages"][pi]["blocks"][bi]
                source, initial, cases = self.extract(document, spec)
                self.assertTrue(source)
                self.assertNotEqual(initial, source, "The reference must still replace the missing implementation")
                self.assertEqual(editor["initialCode"], initial)
                self.assertEqual(editor["outputCheck"]["cases"], cases)
                self.assertEqual(before, document)

    def test_valid_insertions_and_reordering_preserve_the_exact_selected_reference(self):
        for spec in verifier.LESSONS:
            with self.subTest(lesson=spec[0]):
                document = json.loads((verifier.CONTENT / spec[0]).read_text(encoding="utf-8"))
                expected = self.extract(document, spec)
                plan = deepcopy(document["stateLayout"]["current"])
                # Move existing page identities and append unrelated code with
                # the same function name, through the real authoring API.
                plan[0], plan[1] = plan[1], plan[0]
                editor_id = targets.EDITOR_OWNERS[spec[0]]
                editor_page = next(record for record in plan if editor_id in record["blocks"])
                reserved = max(len(member["origin"]) for member in document["stateLayout"]["family"].values())
                slot = max([reserved] + [item["storage"]["page"] + 1
                                         for item in document["stateLayout"]["additions"]])
                additions = [
                    {"id": "qa_unrelated_source", "storage": {"page": slot, "block": 0},
                     "progressPage": plan[0]["id"],
                     "block": {"type": "code", "language": "cpp",
                               "code": "// Unrelated code with the same function name\nvoid " + spec[1] + "() {}\n"}},
                    {"id": "qa_intro_text", "storage": {"page": slot, "block": 1},
                     "progressPage": editor_page["id"],
                     "block": {"type": "paragraph", "text": "Nueva introducción ajena al ejercicio"}},
                    {"id": "qa_other_editor", "storage": {"page": slot, "block": 2},
                     "progressPage": editor_page["id"],
                     "block": {"type": "cpp_compiler",
                               "initialCode": "// Another activity with the same function name\nvoid " +
                                              spec[1] + "() {}\nint main() {}\n"}},
                ]
                plan[0]["blocks"].insert(0, "qa_unrelated_source")
                editor_page["blocks"].insert(0, "qa_intro_text")
                editor_page["blocks"].insert(0, "qa_other_editor")
                changed = state.reorder(document, plan, additions=additions)
                before = deepcopy(changed)
                self.assertEqual(expected, self.extract(changed, spec))
                self.assertEqual(before, changed)

    def test_declared_owner_cannot_fall_back_to_another_valid_editor(self):
        document = mapped_fixture()
        filename = verifier.LESSONS[0][0]
        # The fixture has a valid compiler with a different identity.
        with self.assertRaisesRegex(ValueError, r"missing declared C\+\+ editor"):
            targets.declared_editor(document, filename=filename)
        with self.assertRaisesRegex(ValueError, "no declared curricular editor"):
            targets.declared_editor(document, filename="unreviewed_lesson.json")
        with patch.dict(targets.EDITOR_OWNERS, {filename: "block_0000_0000"}):
            with self.assertRaisesRegex(ValueError, r"is not a C\+\+ editor"):
                targets.declared_editor(document, filename=filename)

    def test_current_u4l7_keeps_its_distinct_editors_and_reference_groups(self):
        spec = verifier.LESSONS[-1]
        document = json.loads((verifier.CONTENT / spec[0]).read_text(encoding="utf-8"))
        blocks = targets.blocks_by_identity(document, filename=spec[0])
        primary = blocks["block_0006_0003"]
        debugging = blocks["u4l7_depurar_editor"]
        self.assertEqual("cpp_compiler", primary["type"])
        self.assertEqual("cpp_compiler", debugging["type"])
        self.assertIn("contar_tipos", primary["initialCode"])
        self.assertIn("contar_tipos", debugging["initialCode"])
        self.assertNotEqual(primary["outputCheck"]["cases"], debugging["outputCheck"]["cases"])
        groups = {entry["activity"]: entry["blocks"] for entry in document["referenceAfter"]}
        self.assertNotEqual(groups["block_0006_0003"], groups["u4l7_depurar_editor"])
        source, initial, cases = self.extract(document, spec)
        self.assertEqual(primary["initialCode"], initial)
        self.assertEqual(primary["outputCheck"]["cases"], cases)
        self.assertNotEqual(debugging["initialCode"], initial)
        self.assertNotIn("total = {};", source)

    def test_missing_or_misaligned_current_maps_do_not_fall_back_to_positions(self):
        mutations = {
            "no layout": lambda d: d.pop("stateLayout"),
            "no current": lambda d: d["stateLayout"].pop("current"),
            "current is not an array": lambda d: d["stateLayout"].update(current={}),
            "missing page": lambda d: d["stateLayout"]["current"].pop(),
            "extra page": lambda d: d["stateLayout"]["current"].append({"id": "extra", "blocks": []}),
            "missing block": lambda d: d["stateLayout"]["current"][0]["blocks"].pop(),
            "extra block": lambda d: d["stateLayout"]["current"][0]["blocks"].append("extra"),
        }
        for name, mutate in mutations.items():
            with self.subTest(mutation=name):
                document = mapped_fixture()
                mutate(document)
                with self.assertRaisesRegex(ValueError, r"stateLayout\.current"):
                    targets.blocks_by_identity(document, filename="fixture.json")

    def test_duplicate_owners_are_rejected_before_dictionary_overwrite(self):
        # The repeated paragraphs have identical payloads, so duplicate owner
        # checks remain necessary even when every fingerprint still matches.
        mutations = {
            "page identity": lambda d: d["stateLayout"]["current"][1].update(id="page_0000"),
            "block in same page": lambda d: d["stateLayout"]["current"][0]["blocks"].__setitem__(1, "block_0000_0000"),
            "block in another page": lambda d: d["stateLayout"]["current"][1]["blocks"].__setitem__(1, "block_0000_0000"),
        }
        for name, mutate in mutations.items():
            with self.subTest(mutation=name):
                document = mapped_fixture()
                mutate(document)
                with self.assertRaisesRegex(ValueError, "identities changed"):
                    targets.blocks_by_identity(document, filename="fixture.json")

    def test_unique_well_formed_owners_cannot_be_assigned_to_other_payloads(self):
        document = mapped_fixture()
        identifiers = document["stateLayout"]["current"][0]["blocks"]
        identifiers[0], identifiers[2] = identifiers[2], identifiers[0]
        with self.assertRaisesRegex(ValueError, "contract changed or belongs to another identity"):
            targets.blocks_by_identity(document, filename="fixture.json")

    def test_malformed_current_records_and_identities_are_rejected(self):
        mutations = {
            "record": lambda d: d["stateLayout"]["current"].__setitem__(0, None),
            "record missing id": lambda d: d["stateLayout"]["current"][0].pop("id"),
            "blocks not an array": lambda d: d["stateLayout"]["current"][0].update(blocks="intro"),
            "invalid page id": lambda d: d["stateLayout"]["current"][0].update(id="../page"),
            "non-string block id": lambda d: d["stateLayout"]["current"][0]["blocks"].__setitem__(0, 7),
            "empty block id": lambda d: d["stateLayout"]["current"][0]["blocks"].__setitem__(0, ""),
            "non-object block": lambda d: d["pages"][0]["blocks"].__setitem__(0, None),
        }
        for name, mutate in mutations.items():
            with self.subTest(mutation=name):
                document = mapped_fixture()
                mutate(document)
                with self.assertRaisesRegex(ValueError, "fixture.json:"):
                    targets.blocks_by_identity(document, filename="fixture.json")

    def test_missing_or_duplicate_authored_reference_remains_an_error(self):
        spec = verifier.LESSONS[1]
        document = json.loads((verifier.CONTENT / spec[0]).read_text(encoding="utf-8"))
        for duplicate in (False, True):
            with self.subTest(duplicate=duplicate):
                changed = deepcopy(document)
                if duplicate:
                    changed["referenceAfter"].append(deepcopy(changed["referenceAfter"][0]))
                    message = "Duplicate referenceAfter activity"
                else:
                    changed["referenceAfter"].clear()
                    message = "missing unique after-attempt reference"
                with self.assertRaisesRegex(ValueError, message):
                    self.extract(changed, spec)


if __name__ == "__main__":
    unittest.main()
