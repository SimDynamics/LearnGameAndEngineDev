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

    def test_all_current_editors_keep_exact_starters_and_cases(self):
        for spec in verifier.LESSONS:
            with self.subTest(lesson=spec[0]):
                document = json.loads((verifier.CONTENT / spec[0]).read_text(encoding="utf-8"))
                before = deepcopy(document)
                editors = [block for page in document["pages"] for block in page["blocks"]
                           if block["type"] == "cpp_compiler"]
                self.assertEqual(1, len(editors))
                source, initial, cases = self.extract(document, spec)
                self.assertTrue(source)
                self.assertNotEqual(initial, source, "The reference must still replace the missing implementation")
                self.assertEqual(editors[0]["initialCode"], initial)
                self.assertEqual(editors[0]["outputCheck"]["cases"], cases)
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
                editor_id = document["referenceAfter"][0]["activity"]
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
                ]
                plan[0]["blocks"].insert(0, "qa_unrelated_source")
                editor_page["blocks"].insert(0, "qa_intro_text")
                changed = state.reorder(document, plan, additions=additions)
                before = deepcopy(changed)
                self.assertEqual(expected, self.extract(changed, spec))
                self.assertEqual(before, changed)

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
                    verifier.blocks_by_identity(document, filename="fixture.json")

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
                    verifier.blocks_by_identity(document, filename="fixture.json")

    def test_unique_well_formed_owners_cannot_be_assigned_to_other_payloads(self):
        document = mapped_fixture()
        identifiers = document["stateLayout"]["current"][0]["blocks"]
        identifiers[0], identifiers[2] = identifiers[2], identifiers[0]
        with self.assertRaisesRegex(ValueError, "contract changed or belongs to another identity"):
            verifier.blocks_by_identity(document, filename="fixture.json")

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
                    verifier.blocks_by_identity(document, filename="fixture.json")

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
