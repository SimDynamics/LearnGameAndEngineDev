"""Native public field selection and anticipation, without synthesis or windows."""
from copy import deepcopy
from importlib import util
import json
from pathlib import Path
import unittest

from test_text_semantics import descriptor, field, units
from test_worker import Worker

ROOT = Path(__file__).resolve().parents[3]
spec = util.spec_from_file_location("visible_semantic_layout", ROOT / "scripts/curriculum/stable_state_layout.py")
layout = util.module_from_spec(spec)
spec.loader.exec_module(layout)


class CurriculumVisibleSemanticsTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.worker = Worker(missing_resources=True)

    @classmethod
    def tearDownClass(cls):
        cls.worker.close()

    def lesson(self, block, definitions=()):
        raw = dict(id="qa-visible-semantics", title="QA", pages=[dict(blocks=[
            dict(type="paragraph", text="Vecino inalterado."), block])])
        before = layout.prepare_v2(raw, "es", {"es": raw})
        lesson = deepcopy(before)
        if definitions:
            owner = lesson["stateLayout"]["current"][0]["blocks"][1]
            lesson["textSemantics"] = dict(version=1, fields=[field(owner, path, source, value)
                for path, source, value in definitions])
        layout.validate(lesson)
        layout.validate_transition(before, lesson)
        self.assertEqual(before["pages"], lesson["pages"])
        self.assertEqual(before["stateLayout"], lesson["stateLayout"])
        self.assertEqual([layout.state_fingerprint(b) for b in before["pages"][0]["blocks"]],
                         [layout.state_fingerprint(b) for b in lesson["pages"][0]["blocks"]])
        return lesson

    def background(self, lesson, ui="es"):
        snapshot = deepcopy(lesson)
        reply = self.worker.ask("curriculum_plan", lessonJson=json.dumps(lesson), page=0,
                                language="es", voice="ef_dora", uiLanguage=ui, readingContext="cpp")
        self.assertEqual(snapshot, lesson)
        return reply["segments"]

    def foreground(self, source, semantics=None):
        request = dict(text=source, language="es", voice="ef_dora", mathMode="auto", readingContext="cpp")
        if semantics is not None:
            request["textSemantics"] = semantics
        return self.worker.ask("plan", **request)["segments"]

    def assert_prepared(self, source, semantics, background):
        foreground = self.foreground(source, semantics)
        keys = {part["key"] for part in background}
        self.assertTrue(foreground, source)
        for part in foreground:
            self.assertIn(part["key"], keys, (source, background))
        raw = source.encode("utf-16-le")
        for part in foreground:
            self.assertEqual(part["text"], raw[2*part["sourceStart"]:2*part["sourceEnd"]].decode("utf-16-le"))
            self.assertEqual(len(part["spokenRanges"]), len(part["displayedRanges"]))

    def test_card_value_is_selected_for_every_native_card_family_and_nested_content(self):
        for kind, group in (("math_multiple_choice_single", "options"), ("math_multiple_choice_multi", "options"),
                            ("math_matching_drag", "targets"), ("math_ordering_drag", "items")):
            for nested in (False, True):
                with self.subTest(kind=kind, nested=nested):
                    card = dict(id="HIDDEN_FALLBACK_ID", value="y")
                    leaf = "/value"
                    if nested:
                        card = dict(id="HIDDEN_FALLBACK_ID", content=[dict(type="rich_answer_option", content=[dict(type="text", formula="y")])])
                        leaf = "/content/0/content/0/formula"
                    response = dict(id="cards", type=kind, **{group: [card]})
                    if kind == "math_matching_drag":
                        response["sources"] = [dict(id="source", label="Origen")]
                    block = dict(type="math_puzzle", schemaVersion=1, prompt="Observa.", responses=[response])
                    explicit = descriptor("y", role="cpp")
                    lesson = self.lesson(block, [(f"/responses/0/{group}/0"+leaf, "y", explicit)])
                    reply = self.background(lesson)
                    self.assert_prepared("y", explicit, reply)
                    self.assertNotIn("FALLBACK", " ".join(part["text"] for part in reply))
                    # No overlay must still anticipate the same visible source,
                    # while keeping its legacy pronunciation policy.
                    self.assert_prepared("y", None, self.background(self.lesson(block)))

    def test_text_representation_uses_first_native_field_with_or_without_config(self):
        for container in ("representations", "tools"):
            for wrapped in (False, True):
                for first in ("latex", "text", "formula", "value"):
                    with self.subTest(container=container, wrapped=wrapped, first=first):
                        names = ["latex", "text", "formula", "value"]
                        values = {name: "y" if name == first else "INVISIBLE_LOWER_PRIORITY" for name in names[names.index(first):]}
                        representation = dict(type="text", config=values) if wrapped else dict(type="text", **values)
                        block = dict(type="math_puzzle", schemaVersion=1, prompt="Observa.", responses=[], **{container:[representation]})
                        path = f"/{container}/0/" + ("config/" if wrapped else "") + first
                        explicit = descriptor("y", role="cpp")
                        reply = self.background(self.lesson(block, [(path, "y", explicit)]))
                        self.assert_prepared("y", explicit, reply)
                        self.assertNotIn("INVISIBLE", " ".join(part["text"] for part in reply))
                        self.assert_prepared("y", None, self.background(self.lesson(block)))
                        if container == "tools":
                            # Native RichEdit representations retain their
                            # public text even if the auxiliary flag is set.
                            representation["interactive"] = True
                            self.assert_prepared("y", explicit,
                                self.background(self.lesson(block, [(path, "y", explicit)])))

    def test_authored_instructions_keep_their_exact_source_and_distinct_owner(self):
        source = "😀 y"
        semantics = descriptor(source, start=units("😀 "), end=units(source))
        for standalone in (False, True):
            with self.subTest(standalone=standalone):
                response = dict(type="math_numeric_input", id="number", instruction=source,
                                validator=dict(type="numeric", expected=1))
                block = dict(response, prompt="Responde.") if standalone else dict(type="math_puzzle", schemaVersion=1,
                    prompt="Responde.", responses=[response])
                path = "/instruction" if standalone else "/responses/0/instruction"
                reply = self.background(self.lesson(block, [(path, source, semantics)]))
                self.assert_prepared(source, semantics, reply)
                self.assert_prepared(source, None, self.background(self.lesson(block)))

    def test_unit_caption_preserves_annotation_through_localized_generated_prefix(self):
        # Bare y is ordinary prose without a descriptor. An authored cpp y
        # names the identifier, so matching this key cannot pass by coincidence.
        source = "y"
        explicit = descriptor(source, role="cpp")
        self.assertNotEqual(self.foreground(source)[0]["key"], self.foreground(source, explicit)[0]["key"])
        for fields in (None, []):
            response = dict(id="number", type="math_numeric_input", unit=source, validator=dict(type="numeric", expected=1))
            if fields is not None:
                response["fields"] = fields
            block = dict(type="math_puzzle", schemaVersion=1, prompt="Responde.", responses=[response])
            lesson = self.lesson(block, [("/responses/0/unit", source, explicit)])
            for ui, prefix in (("es", "Unidad: "), ("en", "Unit: "), ("fr", "Unité : ")):
                with self.subTest(ui=ui, fields=fields):
                    whole = prefix+source
                    semantics = dict(spans=[dict(explicit["spans"][0], start=units(prefix), end=units(whole))])
                    self.assert_prepared(whole, semantics, self.background(lesson, ui))

    def test_current_field_selection_does_not_change_neighbors_or_direct_plans(self):
        block = dict(type="math_puzzle", schemaVersion=1, prompt="Observa.", representations=[dict(type="text", formula="y")], responses=[])
        explicit = descriptor("y", role="cpp")
        lesson = self.lesson(block, [("/representations/0/formula", "y", explicit)])
        before = self.foreground("Vecino inalterado.")
        reply = self.background(lesson)
        self.assert_prepared("Vecino inalterado.", None, reply)
        self.assertEqual(before, self.foreground("Vecino inalterado."))
        self.assertEqual(self.background(lesson), reply)


if __name__ == "__main__":
    unittest.main()
