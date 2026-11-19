"""Verify a real published choice can receive a general authenticated text edit.

The lesson stays untouched. Work owns the final curricular wording and release.
This fixture exercises the existing authoring, state, semantic and curriculum
validators on a *copy* of the actual Programación 1 publication.
"""
from __future__ import annotations

from copy import deepcopy
import json
from pathlib import Path
import unittest

import stable_state_layout as state
import validar_json_programacion as curriculum


ROOT = Path(__file__).resolve().parents[2]
RELATIVE = (
    "app/src/main/assets/content/es/programming/cpp_fundamentals/"
    "datos_propios_y_estado_valido/lesson_construir_y_comprobar_un_modelo_de_pedidos.json"
)
PATH = ROOT / RELATIVE
OWNER = "block_0007_0002"
OLD_TEXT = "La operación incumple su precondición aunque conserve una cantidad válida."
NEW_TEXT = (
    "La operación incumple la condición para aceptar la transición, "
    "aunque conserve una cantidad válida."
)


def owner_location(document: dict) -> tuple[int, int]:
    found = [
        (page, block)
        for page, (payload, layout) in enumerate(zip(
            document["pages"], document["stateLayout"]["current"]))
        for block, _ in enumerate(payload["blocks"])
        if layout["blocks"][block] == OWNER
    ]
    if len(found) != 1:
        raise AssertionError("The real choice owner was lost or duplicated")
    return found[0]


def owner_block(document: dict) -> dict:
    page, block = owner_location(document)
    return document["pages"][page]["blocks"][block]


class RealChoiceEditorialRevision(unittest.TestCase):
    @classmethod
    def setUpClass(cls) -> None:
        cls.original_bytes = PATH.read_bytes()
        cls.before = json.loads(cls.original_bytes)
        before_block = owner_block(cls.before)
        if before_block.get("type") != "multiple_choice" or before_block["options"][0] != OLD_TEXT:
            raise AssertionError("Published initial choice differs from the authentic curriculum")
        if before_block["correctIndex"] != 0:
            raise AssertionError("Published answer position is no longer zero")
        cls.origin = state.validate(cls.before)[owner_location(cls.before)]
        # Explicit [] marks ordinary, unhighlighted option prose; it is not
        # permission to inherit the previous option's reading or color spans.
        cls.after = state.revise_choice_editorial(cls.before, OWNER, 0, NEW_TEXT, spans=[])

    def test_current_publication_accepts_the_exact_general_revision(self) -> None:
        state.validate_transition(self.before, self.after)
        self.assertTrue(curriculum.preserves_general_choice_option_revision(
            RELATIVE, *self.origin, owner_block(self.before), owner_block(self.after),
            previous_document=self.before, current_document=self.after,
            course_id="cpp_fundamentals"))
        self.assertEqual(state._historical_choice_contract(
            self.after, OWNER, owner_block(self.after)), owner_block(self.before))
        self.assertEqual(owner_block(self.after)["correctIndex"], 0)
        self.assertEqual(owner_block(self.after)["options"][1:],
                         owner_block(self.before)["options"][1:])
        self.assertEqual(owner_block(self.after)["reviewFeedback"],
                         owner_block(self.before)["reviewFeedback"])
        self.assertEqual(owner_block(self.after)["explanation"],
                         owner_block(self.before)["explanation"])

    def test_history_and_other_course_activities_are_untouched(self) -> None:
        old, new = self.before["stateLayout"], self.after["stateLayout"]
        for field in ("origin", "current", "family", "additions", "retired",
                      "retirementHistory", "sourceFormatRevisions"):
            self.assertEqual(old.get(field), new.get(field), field)
        self.assertEqual(new["editorial"][:-1], old["editorial"])
        self.assertEqual(new.get("choiceRevisions", [])[:-1],
                         old.get("choiceRevisions", []))
        archived = new["choiceRevisions"][-1]
        self.assertEqual(archived["id"], OWNER)
        self.assertEqual(archived["block"], owner_block(self.before))
        self.assertEqual(archived["optionIndex"], 0)
        self.assertEqual(archived["replacement"], NEW_TEXT)
        self.assertEqual(archived["semanticBefore"],
                         next(item for item in self.before["textSemantics"]["fields"]
                              if (item["block"], item["path"]) == (OWNER, "/options/0")))
        current = deepcopy(self.after)
        current["stateLayout"] = deepcopy(self.before["stateLayout"])
        current["textSemantics"] = deepcopy(self.before["textSemantics"])
        owner_block(current)["options"][0] = OLD_TEXT
        self.assertEqual(current, self.before)
        self.assertEqual(PATH.read_bytes(), self.original_bytes)

    def test_changes_to_answer_guides_or_another_option_are_rejected(self) -> None:
        def wrong_correct_index(block: dict) -> None:
            block["correctIndex"] = 1

        def wrong_other_option(block: dict) -> None:
            block["options"][1] += " (alterada)"

        def wrong_feedback(block: dict) -> None:
            block["reviewFeedback"][0] += " (alterada)"

        for description, mutate in (
            ("correctIndex", wrong_correct_index),
            ("other option", wrong_other_option),
            ("feedback", wrong_feedback),
        ):
            with self.subTest(change=description):
                forged = deepcopy(self.after)
                mutate(owner_block(forged))
                with self.assertRaises(state.LayoutError):
                    state.validate_transition(self.before, forged)

    def test_forged_semantic_or_historical_archive_is_rejected(self) -> None:
        for description in ("wrong text hash", "foreign archived answer"):
            with self.subTest(change=description):
                forged = deepcopy(self.after)
                if description == "wrong text hash":
                    marker = next(item for item in forged["textSemantics"]["fields"]
                                  if (item["block"], item["path"]) == (OWNER, "/options/0"))
                    marker["sourceSha256"] = "0" * 64
                else:
                    forged["stateLayout"]["choiceRevisions"][-1]["block"]["correctIndex"] = 2
                with self.assertRaises(state.LayoutError):
                    state.validate_transition(self.before, forged)


if __name__ == "__main__":
    unittest.main()
