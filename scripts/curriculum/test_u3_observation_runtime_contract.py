"""Original U3L8 reports: declarations preserve their editors and state."""
import copy
import unittest
from pathlib import Path

import stable_state_layout as layout

ROOT = Path(__file__).resolve().parents[2]
LESSON = (ROOT / "app/src/main/assets/content/es/programming/programming_practice_tools"
          / "comprobaciones_repetibles/lesson_automatizar_la_revision_sin_borrar_evidencia.json")
RECORDS = [
    {"activity": "block_0003_0003", "kind": "observationCompleteV1"},
    {"activity": "block_0006_0003", "kind": "observationDifferencesV1"},
]


def historical_fixture(current):
    """Build the pre-declaration QA model from a validated current lesson."""
    layout.validate(current)
    if "compilerRuntimeChecks" in current and current["compilerRuntimeChecks"] != RECORDS:
        raise layout.LayoutError("Unexpected published U3L8 runtime contract")
    original = copy.deepcopy(current)
    original.pop("compilerRuntimeChecks", None)
    layout.validate(original)
    return original


class ObservationRuntimeDeclarationTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.current = layout.load(LESSON)
        cls.original = historical_fixture(cls.current)

    def declared(self):
        current = copy.deepcopy(self.original)
        current["compilerRuntimeChecks"] = copy.deepcopy(RECORDS)
        return current

    def test_fixture_accepts_an_already_published_declaration_without_mutation(self):
        published = self.declared()
        preserved = copy.deepcopy(published)
        historical = historical_fixture(published)
        self.assertEqual(self.original, historical)
        self.assertEqual(preserved, published)
        self.assertEqual(published["pages"], historical["pages"])
        self.assertEqual(published["stateLayout"], historical["stateLayout"])

    def test_fixture_does_not_hide_an_unexpected_published_contract(self):
        for records in (None, [], list(reversed(RECORDS)),
                        [{"activity": "block_0003_0003", "kind": "nonBlank"}]):
            with self.subTest(records=records):
                current = self.declared()
                current["compilerRuntimeChecks"] = copy.deepcopy(records)
                preserved = copy.deepcopy(current)
                with self.assertRaises(layout.LayoutError):
                    historical_fixture(current)
                self.assertEqual(preserved, current)

    def test_two_original_editors_preserve_all_blocks_and_state(self):
        revised = self.declared()
        self.assertEqual(layout.validate(self.original), layout.validate(revised))
        layout.validate_transition(self.original, revised)
        self.assertEqual(self.original["pages"], revised["pages"])
        self.assertEqual(self.original["stateLayout"], revised["stateLayout"])
        for page, identity in ((3, "block_0003_0003"), (6, "block_0006_0003")):
            self.assertEqual(identity, revised["stateLayout"]["current"][page]["blocks"][3])

    def test_only_exact_pair_order_id_and_kind_are_allowed(self):
        for variant in (
            lambda doc: doc["compilerRuntimeChecks"].reverse(),
            lambda doc: doc["compilerRuntimeChecks"].pop(),
            lambda doc: doc["compilerRuntimeChecks"].append(RECORDS[0]),
            lambda doc: doc["compilerRuntimeChecks"][0].update(kind="nonBlank"),
            lambda doc: doc["compilerRuntimeChecks"][1].update(activity="block_0006_0002"),
            lambda doc: doc["compilerRuntimeChecks"][1].update(extra=True),
        ):
            with self.subTest(line=variant.__code__.co_firstlineno):
                changed = self.declared()
                variant(changed)
                with self.assertRaises(layout.LayoutError):
                    layout.validate(changed)

    def test_parser_prepared_input_and_original_code_cannot_be_replaced(self):
        for page, edit in (
            (3, lambda block: block.update(initialStandardInput="A 0 0")),
            (6, lambda block: block.update(initialStandardInput="N 0 0")),
            (3, lambda block: block.update(initialCode="int main() { return 0; }")),
            (6, lambda block: block.update(outputCheck={"cases": []})),
        ):
            with self.subTest(page=page, line=edit.__code__.co_firstlineno):
                revised = self.declared()
                edit(revised["pages"][page]["blocks"][3])
                with self.assertRaises(layout.LayoutError):
                    layout.validate(revised)

    def test_removing_a_published_declaration_is_not_compatible(self):
        with self.assertRaises(layout.LayoutError):
            layout.validate_transition(self.declared(), self.original)


if __name__ == "__main__":
    unittest.main()
