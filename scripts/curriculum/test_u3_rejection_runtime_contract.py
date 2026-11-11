"""Opt-in U3L6 semantic diagnostics without changing five historical editor files."""
import copy
import unittest
from pathlib import Path

import stable_state_layout as layout

ROOT = Path(__file__).resolve().parents[2]
LESSON = (ROOT / "app/src/main/assets/content/es/programming/programming_practice_tools"
          / "comprobaciones_repetibles/lesson_diagnosticos_para_corregir_una_peticion.json")
RECORDS = [
    {"activity": "block_0003_0003", "kind": "semanticRejectionBoxesV1"},
    {"activity": "block_0006_0003", "kind": "semanticRejectionBalanceV1"},
]


class RejectionRuntimeDeclarationTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.original = layout.load(LESSON)

    def declared(self):
        current = copy.deepcopy(self.original)
        current["compilerRuntimeChecks"] = copy.deepcopy(RECORDS)
        return current

    def test_two_original_editors_are_declared_without_rewriting_source_or_state(self):
        revised = self.declared()
        self.assertEqual(layout.validate(self.original), layout.validate(revised))
        layout.validate_transition(self.original, revised)
        self.assertEqual(self.original["pages"], revised["pages"])
        self.assertEqual(self.original["stateLayout"], revised["stateLayout"])
        for pn, identity in ((3, "block_0003_0003"), (6, "block_0006_0003")):
            self.assertEqual(identity, revised["stateLayout"]["current"][pn]["blocks"][3])

    def test_only_reviewed_id_kind_and_complete_pair_are_supported(self):
        for variant in (
            lambda doc: doc["compilerRuntimeChecks"].reverse(),
            lambda doc: doc["compilerRuntimeChecks"].pop(),
            lambda doc: doc["compilerRuntimeChecks"].append(RECORDS[0]),
            lambda doc: doc["compilerRuntimeChecks"][0].update(kind="nonBlank"),
            lambda doc: doc["compilerRuntimeChecks"][0].update(activity="block_0003_0002"),
            lambda doc: doc["compilerRuntimeChecks"][1].update(extra=True),
        ):
            with self.subTest(variant=str(variant)):
                edited = self.declared()
                variant(edited)
                with self.assertRaises(layout.LayoutError):
                    layout.validate(edited)

    def test_original_starters_input_and_compiler_address_remain_protected(self):
        for page, change in (
            (3, lambda block: block.update(initialStandardInput="-1 0 21 5")),
            (6, lambda block: block.update(initialStandardInput="2 3")),
            (3, lambda block: block.update(initialCode="int main() { return 0; }")),
            (6, lambda block: block.update(outputCheck={"cases": []})),
        ):
            edited = self.declared()
            change(edited["pages"][page]["blocks"][3])
            with self.subTest(page=page,change=change.__code__.co_firstlineno):
                with self.assertRaises(layout.LayoutError):
                    layout.validate(edited)

    def test_published_runtime_declaration_may_not_be_removed(self):
        revised = self.declared()
        with self.assertRaises(layout.LayoutError):
            layout.validate_transition(revised, self.original)


if __name__ == "__main__":
    unittest.main()
