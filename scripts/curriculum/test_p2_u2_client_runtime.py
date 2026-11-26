"""Closed EOF client diagnostic: root-only authoring preserves historical state."""
from copy import deepcopy
import importlib.util
import json
from pathlib import Path
import unittest

HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[1]
spec = importlib.util.spec_from_file_location("stable_state_layout", HERE / "stable_state_layout.py")
layout = importlib.util.module_from_spec(spec)
spec.loader.exec_module(layout)

ASSET = (ROOT / "app/src/main/assets/content/es/programming/programming_practice_tools"
         / "construccion_reproducible/lesson_opciones_y_revision_de_la_configuracion.json")
DECLARATION = {"activity": "block_0009_0008", "kind": "exactOriginalProjectRuntimeV1"}


class OriginalClientEofContract(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.published = json.loads(ASSET.read_text(encoding="utf-8"))
        assert cls.published.get("compilerRuntimeChecks") == [DECLARATION]
        cls.original = deepcopy(cls.published)
        del cls.original["compilerRuntimeChecks"]

    def test_published_lesson_matches_the_reviewed_single_case(self):
        self.assertEqual(self.published, self.proposed())
        self.assertEqual(len(self.published["compilerRuntimeChecks"]), 1)

    def proposed(self):
        revised = deepcopy(self.original)
        revised["compilerRuntimeChecks"] = [DECLARATION.copy()]
        return revised

    def test_new_eof_goal_preserves_every_original_block_and_storage_key(self):
        before, after = self.original, self.proposed()
        self.assertEqual(layout.validate(before), layout.validate(after))
        layout.validate_transition(before, after)
        self.assertEqual(before["pages"], after["pages"])
        self.assertEqual(before["stateLayout"], after["stateLayout"])
        self.assertEqual(before.get("textSemantics"), after.get("textSemantics"))
        self.assertEqual(after["stateLayout"]["current"][9]["blocks"][8],
                         DECLARATION["activity"])
        owner = after["pages"][9]["blocks"][8]
        self.assertNotIn("outputCheck", owner)
        self.assertNotIn("compileCheck", owner)
        self.assertEqual("6", owner["initialStandardInput"])
        self.assertEqual("OFF", owner["cmake"]["options"]["ACEPTAR_IGUAL"])

    def test_published_goal_cannot_be_removed_or_rewritten(self):
        with self.assertRaisesRegex(layout.LayoutError, "compiler runtime"):
            layout.validate_transition(self.proposed(), self.original)
        updated = self.proposed()
        previous = deepcopy(updated)
        updated["compilerRuntimeChecks"][0]["kind"] = "noSourceGuard"
        with self.assertRaises(layout.LayoutError):
            layout.validate_transition(previous, updated)

    def test_owner_shape_locale_and_kind_are_closed(self):
        variants = (
            [{"activity": "block_0009_0007", "kind": DECLARATION["kind"]}],
            [{"activity": DECLARATION["activity"], "kind": "stdoutOnly"}],
            [DECLARATION, DECLARATION],
            [{"activity": DECLARATION["activity"]}],
            [{"activity": DECLARATION["activity"], "kind": DECLARATION["kind"], "extra": True}],
            "block_0009_0008",
        )
        for value in variants:
            with self.subTest(checks=value):
                data = self.proposed()
                data["compilerRuntimeChecks"] = value
                with self.assertRaises(layout.LayoutError):
                    layout.validate(data)
        changed = self.proposed()
        changed["id"] = "unrelated_lesson"
        with self.assertRaises(layout.LayoutError):
            layout.validate(changed)
        changed = self.proposed()
        changed["stateLayout"]["locale"] = "fr"
        with self.assertRaises(layout.LayoutError):
            layout.validate(changed)

    def test_falsifying_source_input_or_cmake_fails_validation(self):
        variants = (
            lambda c: c["pages"][9]["blocks"][8].update(initialStandardInput="6 6"),
            lambda c: c["pages"][9]["blocks"][8]["cmake"]["options"].update(ACEPTAR_IGUAL="ON"),
            lambda c: c["pages"][9]["blocks"][8]["projectFiles"][4].update(initialCode="int main(){return 2;}"),
            lambda c: c["pages"][9]["blocks"][8].update(outputCheck={"cases":[]}),
        )
        for change in variants:
            with self.subTest(change=change.__code__.co_firstlineno):
                data = self.proposed()
                change(data)
                with self.assertRaises(layout.LayoutError):
                    layout.validate(data)


class ComposedCotizacionesPublicationTest(unittest.TestCase):
    def test_joint_case_and_existing_paragraph_publication(self):
        import cotizaciones_continuity_fixture as fixture
        for prose_first in (False, True):
            with self.subTest(prose_first=prose_first):
                before, middle, after = fixture.stages(prose_first=prose_first)
                layout.validate_publication_transition(before, after)
                self.assertEqual(layout.validate(before), layout.validate(after))
                self.assertEqual(before["stateLayout"]["current"], after["stateLayout"]["current"])
                self.assertEqual(before["stateLayout"]["additions"], after["stateLayout"]["additions"])
                self.assertEqual(before["pages"][:11], after["pages"][:11])
                self.assertEqual(before["pages"][12:], after["pages"][12:])
                self.assertEqual(before["compilerCmakeGraphChecks"], after["compilerCmakeGraphChecks"])
                self.assertEqual(before["compilerRuntimeChecks"], after["compilerRuntimeChecks"])
                self.assertEqual(before["textSemantics"]["rootFields"], after["textSemantics"]["rootFields"])
                owner = layout._blocks(after)[fixture.OWNER]
                self.assertEqual([case["standardInput"] for case in owner["outputCheck"]["cases"]],
                                 ["2", "0", "5", ""])
                self.assertEqual(owner["outputCheck"]["cases"][-1],
                                 layout._blocks(before)[fixture.OWNER]["outputCheck"]["cases"][0])

    def test_joint_publication_rejects_unrelated_or_unauthenticated_semantics(self):
        import cotizaciones_continuity_fixture as fixture
        before, _, after = fixture.stages()

        def marker(doc, owner, path="/text"):
            return next(field for field in doc["textSemantics"]["fields"]
                        if (field["block"], field["path"]) == (owner, path))

        unchanged = deepcopy(after)
        untouched = next(field for field in unchanged["textSemantics"]["fields"]
                         if field["spans"] and field["block"] not in (*fixture.PARAGRAPHS, fixture.OWNER))
        untouched["spans"] = []
        removed = deepcopy(after)
        removed["textSemantics"]["fields"].remove(marker(removed, fixture.PARAGRAPHS[0]))
        erased = deepcopy(after)
        marker(erased, fixture.PARAGRAPHS[0])["spans"] = []
        stale = deepcopy(after)
        marker(stale, fixture.PARAGRAPHS[1])["sourceSha256"] = marker(before, fixture.PARAGRAPHS[1])["sourceSha256"]
        unproved = deepcopy(after)
        unproved["stateLayout"]["editorial"].pop()
        root = deepcopy(after)
        root["textSemantics"]["rootFields"] = []
        forged = deepcopy(after)
        forged["stateLayout"]["compilerCaseRevisions"][-1]["semanticBefore"] = []
        answer = deepcopy(after)
        choice = next(block for block in layout._blocks(answer).values()
                      if block.get("type") == "multiple_choice")
        choice["correctIndex"] = (choice["correctIndex"] + 1) % len(choice["options"])
        for name, invalid in (("unchanged", unchanged), ("removed", removed), ("erased", erased),
                              ("stale", stale), ("unproved", unproved), ("root", root),
                              ("case archive", forged), ("answer", answer)):
            with self.subTest(name=name), self.assertRaises(ValueError):
                layout.validate_publication_transition(before, invalid)


if __name__ == "__main__":
    unittest.main()
