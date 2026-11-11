"""WASI U3L9: root-only, ordered assertion evidence without rewriting editors."""
import copy
import unittest
from pathlib import Path

import stable_state_layout as layout

ROOT = Path(__file__).resolve().parents[2]
LESSON = (ROOT / "app/src/main/assets/content/es/programming/programming_practice_tools"
          / "comprobaciones_repetibles/lesson_comprobar_acuerdos_internos_con_aserciones.json")
RECORDS = [
    {"activity": "block_0002_0003", "kind": "wasiAssertionAbortV1"},
    {"activity": "block_0003_0007", "kind": "wasiAssertionSuppressedV1"},
    {"activity": "block_0004_0007", "kind": "wasiAssertionRepairV1"},
]


class AssertionRuntimeDeclarationTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.published = layout.load(LESSON)
        assert cls.published.get("compilerRuntimeChecks") == RECORDS
        cls.original = copy.deepcopy(cls.published)
        del cls.original["compilerRuntimeChecks"]

    def declared(self):
        revised = copy.deepcopy(self.original)
        revised["compilerRuntimeChecks"] = copy.deepcopy(RECORDS)
        return revised

    def test_all_three_mapped_editors_and_historical_slots_remain_unchanged(self):
        revised = self.declared()
        self.assertEqual(self.published, revised)
        self.assertEqual(layout.validate(self.original), layout.validate(revised))
        layout.validate_transition(self.original, revised)
        self.assertEqual(self.original["pages"], revised["pages"])
        self.assertEqual(self.original["stateLayout"], revised["stateLayout"])
        for page, index, identity in ((2, 3, "block_0002_0003"),
                                      (3, 7, "block_0003_0007"),
                                      (4, 3, "block_0004_0007")):
            self.assertEqual(identity, revised["stateLayout"]["current"][page]["blocks"][index])

    def test_unknown_missing_reordered_or_overwritten_runtime_goals_fail(self):
        for edit in (
            lambda doc: doc["compilerRuntimeChecks"].reverse(),
            lambda doc: doc["compilerRuntimeChecks"].pop(),
            lambda doc: doc["compilerRuntimeChecks"].append(RECORDS[0]),
            lambda doc: doc["compilerRuntimeChecks"][0].update(kind="nonZeroExit"),
            lambda doc: doc["compilerRuntimeChecks"][1].update(activity="block_0003_0006"),
            lambda doc: doc["compilerRuntimeChecks"][2].update(extra=True),
        ):
            with self.subTest(line=edit.__code__.co_firstlineno):
                revised = self.declared()
                edit(revised)
                with self.assertRaises(layout.LayoutError):
                    layout.validate(revised)

    def test_ndebug_assertion_and_initial_input_cannot_be_silently_changed(self):
        for page, index, edit in (
            (2, 3, lambda b: b.update(initialStandardInput="5")),
            (3, 7, lambda b: b.update(initialCode=b["initialCode"].replace("#define NDEBUG\n", ""))),
            (4, 3, lambda b: b.update(initialCode=b["initialCode"].replace(
                "assert(cajas * piezas_por_caja >= piezas);", "// removed"))),
            (4, 3, lambda b: b.update(outputCheck={"cases": []})),
        ):
            with self.subTest(page=page,index=index,line=edit.__code__.co_firstlineno):
                revised = self.declared()
                edit(revised["pages"][page]["blocks"][index])
                with self.assertRaises(layout.LayoutError):
                    layout.validate(revised)

    def test_published_assertion_contract_may_not_disappear(self):
        with self.assertRaises(layout.LayoutError):
            layout.validate_transition(self.declared(), self.original)


if __name__ == "__main__":
    unittest.main()
