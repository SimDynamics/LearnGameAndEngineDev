"""Anticipation must use each source actually owned by a native reading region."""
from copy import deepcopy
from importlib import util
import json
from pathlib import Path
import unittest

from test_text_semantics import descriptor, field, units
from test_worker import Worker

ROOT = Path(__file__).resolve().parents[3]
spec = util.spec_from_file_location("region_identity_layout", ROOT / "scripts/curriculum/stable_state_layout.py")
layout = util.module_from_spec(spec)
spec.loader.exec_module(layout)


class CurriculumRegionIdentityTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.worker = Worker(missing_resources=True)

    @classmethod
    def tearDownClass(cls):
        cls.worker.close()

    def lesson(self, block, definitions):
        raw = dict(id="qa-native-region-identity", pages=[dict(blocks=[block])])
        before = layout.prepare_v2(raw, "es", {"es": raw})
        lesson = deepcopy(before)
        if definitions:
            owner = lesson["stateLayout"]["current"][0]["blocks"][0]
            lesson["textSemantics"] = dict(version=1, fields=[field(owner, path, source, value)
                for path, source, value in definitions])
        layout.validate(lesson)
        layout.validate_transition(before, lesson)
        self.assertEqual(before["pages"], lesson["pages"])
        self.assertEqual(before["stateLayout"], lesson["stateLayout"])
        return lesson

    def assert_native_regions(self, block, definitions, regions, *, ui="es", language="es", content_language=None,
                              reading_context="auto"):
        lesson = self.lesson(block, definitions)
        snapshot = deepcopy(lesson)
        request = dict(lessonJson=json.dumps(lesson), page=0, language=language, voice="ef_dora",
                       uiLanguage=ui, readingContext="cpp")
        if content_language is not None:
            request["contentLanguage"] = content_language
        reply = self.worker.ask("curriculum_plan", **request)
        self.assertEqual(snapshot, lesson)
        keys = {item["key"] for item in reply["segments"]}
        for source, semantics in regions:
            request = dict(text=source, language=language, voice="ef_dora", readingContext=reading_context, mathMode="auto")
            if semantics is not None:
                request["textSemantics"] = semantics
            expected = self.worker.ask("plan", **request)["segments"]
            for item in expected:
                self.assertIn(item["key"], keys, dict(nativeSource=source, expected=item["preparedText"],
                    anticipated=[part["preparedText"] for part in reply["segments"]]))
        return reply

    def test_representation_table_cells_are_independent_native_rich_edits(self):
        block = dict(type="math_puzzle", schemaVersion=1, prompt="Observa.", responses=[],
                     representations=[dict(type="math_table", config=dict(headers=["y", "x"], rows=[]))])
        definitions = [(f"/representations/0/config/headers/{at}", source, descriptor(source))
                       for at, source in enumerate(("y", "x"))]
        self.assert_native_regions(block, definitions, [(source, value) for _, source, value in definitions])

    def test_current_programming_field_table_headers_are_independent_native_regions(self):
        # This exact public header pair occurs in Programación 1's
        # aceptar_registros_y_detectar_duplicados response table.
        block = dict(type="math_puzzle", schemaVersion=1, prompt="Completa.", responses=[dict(id="q",
            type="math_multi_input", fields=[dict(id="a", type="numeric", label="Cantidad")],
            layout="table", headers=["Resultado", "Valor"], rows=[["Registro", dict(field="a")]],
            validator=dict(type="fields", fields=dict(a=dict(type="numeric", expected="1"))))])
        self.assert_native_regions(block, [], [("Resultado", None), ("Valor", None), ("Registro", None), ("Cantidad", None)])

    def test_current_nested_component_modes_keep_their_real_programming_recording_keys(self):
        base = ROOT / "app/src/main/assets/content/es/programming/cpp_fundamentals"
        cases = [
            ("archivos_y_programa_completo/lesson_precision_y_presentacion_de_double.json", 1, 2,
             "/responses/0/targets/1/content/0"),
            ("repeticion_y_terminacion/lesson_despacho_y_recuperacion_de_recorridos.json", 5, 4,
             "/responses/0/sources/0/content/0"),
            ("archivos_y_programa_completo/lesson_aceptar_registros_y_detectar_duplicados.json", 2, 4,
             "/responses/1/options/0/content/0"),
        ]
        for relative, page_index, block_index, path in cases:
            with self.subTest(asset=relative, path=path):
                asset = base / relative
                original = asset.read_bytes(); lesson = json.loads(original.decode("utf-8-sig"))
                block = lesson["pages"][page_index]["blocks"][block_index]
                component = block
                for token in path.split("/")[1:]:
                    component = component[int(token)] if isinstance(component, list) else component[token]
                config = component.get("config", component)
                source = config["text"]
                # This is the mode retained by MathPuzzleView's label_formats;
                # the containing block has a distinct, default auto mode.
                mode = config.get("mathMode", "auto")
                self.assertEqual("text", mode)
                self.assertEqual("auto", block.get("mathMode", "auto"))
                owner = lesson["stateLayout"]["current"][page_index]["blocks"][block_index]
                field_path = path + ("/config" if "config" in component else "") + "/text"
                field = next((item for item in lesson.get("textSemantics", {}).get("fields", [])
                              if item["block"] == owner and item["path"] == field_path), None)
                direct = dict(text=source, language="es", voice="ef_dora", readingContext="auto", mathMode=mode)
                if field is not None:
                    direct["textSemantics"] = dict(spans=field["spans"])
                expected = self.worker.ask("plan", **direct)["segments"]
                background = self.worker.ask("curriculum_plan", lessonJson=original.decode("utf-8-sig"), page=page_index,
                    language="es", voice="ef_dora", uiLanguage="es", contentLanguage="es", readingContext="cpp")["segments"]
                keys = {item["key"] for item in background}
                for item in expected:
                    self.assertIn(item["key"], keys)
                old = self.worker.ask("plan", **dict(direct, mathMode="auto"))["segments"]
                if field is None and source == "3.125":
                    self.assertEqual(["3.125"], [item["spokenText"] for item in expected])
                    self.assertEqual(["tres coma uno dos cinco"], [item["spokenText"] for item in old])
                    self.assertNotEqual([item["key"] for item in old], [item["key"] for item in expected])
                else:
                    self.assertEqual([item["key"] for item in old], [item["key"] for item in expected])
                self.assertEqual(original, asset.read_bytes())

    def test_field_caption_and_unit_keep_their_separate_native_source_owners(self):
        block = dict(type="math_puzzle", schemaVersion=1, prompt="Completa.", responses=[dict(id="q",
            type="math_multi_input", fields=[dict(id="a", type="numeric", label="x", unit="y")],
            validator=dict(type="fields", fields=dict(a=dict(type="numeric", expected="1"))))])
        definitions = [("/responses/0/fields/0/label", "x", descriptor("x")),
                       ("/responses/0/fields/0/unit", "y", descriptor("y"))]
        self.assert_native_regions(block, definitions, [(source, value) for _, source, value in definitions])

    def test_inline_segments_and_field_caption_are_their_own_native_regions(self):
        block = dict(type="math_puzzle", schemaVersion=1, prompt="Completa.", responses=[dict(id="q",
            type="math_multi_input", fields=[dict(id="a", type="numeric", label="Campo")],
            segments=[dict(text="y "), dict(field="a"), dict(text=" x")],
            validator=dict(type="fields", fields=dict(a=dict(type="numeric", expected="1"))))])
        definitions = [("/responses/0/segments/0/text", "y ", descriptor("y ")),
                       ("/responses/0/fields/0/label", "Campo", {"spans": []}),
                       ("/responses/0/segments/2/text", " x", descriptor(" x"))]
        self.assert_native_regions(block, definitions, [(source, value) for _, source, value in definitions])

    def test_partial_region_annotation_does_not_force_other_cells_or_field_parts(self):
        block = dict(type="math_puzzle", schemaVersion=1, prompt="Observa.", responses=[],
                     representations=[dict(type="math_table", headers=["y", "Prosa vecina"], rows=[])])
        value = descriptor("y")
        self.assert_native_regions(block, [("/representations/0/headers/0", "y", value)],
                                   [("y", value), ("Prosa vecina", None)])
        response = dict(id="q", type="math_multi_input", fields=[dict(id="a", type="fixed", label="Rótulo", text="y", unit="unidades")],
                        validator=dict(type="fields", fields=dict(a=dict(type="numeric", expected="1"))))
        block = dict(type="math_puzzle", schemaVersion=1, prompt="Observa.", responses=[response])
        self.assert_native_regions(block, [("/responses/0/fields/0/text", "y", value)],
                                   [("Rótulo", None), ("y", value), ("unidades", None)])

    @staticmethod
    def feedback_descriptor(guide, value, prefix, trim=0):
        raw = guide.encode("utf-16-le")
        return {"spans": [dict(span, start=units(prefix)+max(span["start"], trim)-trim,
                                     end=units(prefix)+span["end"]-trim,
                                     text=raw[2*max(span["start"], trim):2*span["end"]].decode("utf-16-le"))
                           for span in value["spans"] if span["end"] > trim]}

    def test_success_feedback_uses_the_actual_native_region_and_its_locale_owners(self):
        guide = "\r\n`y`."
        value = descriptor(guide, start=units("\r\n`"), end=units("\r\n`y"))
        for content, ui, speech in (("es", "fr", "es"), ("fr", "es", "es"), ("es", "en", "en-US")):
            with self.subTest(kind="math", content=content, ui=ui, speech=speech):
                block = dict(type="math_puzzle", schemaVersion=1, prompt="Observa.", feedback=dict(correct=guide, incorrect="HIDDEN_INCORRECT"),
                             responses=[dict(id="q", type="math_multiple_choice_single", options=[dict(id="a", label="A")],
                                             validator=dict(type="choice", expected=["a"]))])
                verdict = self.worker.ask("validate_math", blockJson=json.dumps(block),
                    responsesJson=json.dumps({"q": dict(type="choice", selected=["a"])}), language=content)
                heading = "✓ Correcto" if content == "es" else "✓ Correct"
                self.assertTrue(verdict["correct"])
                self.assertEqual(heading+"\n"+guide, verdict["feedback"])
                whole = self.feedback_descriptor(guide, value, heading+"\n")
                definitions = [("/feedback/correct", guide, value), ("/feedback/incorrect", "HIDDEN_INCORRECT", {"spans": []})]
                reply = self.assert_native_regions(block, definitions, [(verdict["feedback"], whole)],
                    ui=ui, language=speech, content_language=content)
                self.assertNotIn("HIDDEN_INCORRECT", " ".join(part["preparedText"] for part in reply["segments"]))
            with self.subTest(kind="ordinary", content=content, ui=ui, speech=speech):
                block = dict(type="multiple_choice", question="Elige.", options=["A", "B"], explanation=guide,
                             reviewFeedback=["HIDDEN_REVIEW_A", "HIDDEN_REVIEW_B"])
                prefix = "✓ " + ("Correcto" if ui == "es" else "Correct") + "\n\n"
                full = prefix+guide.lstrip("\r\n")
                whole = self.feedback_descriptor(guide, value, prefix, trim=units(guide)-units(guide.lstrip("\r\n")))
                reply = self.assert_native_regions(block, [("/explanation", guide, value)], [(full, whole)],
                    ui=ui, language=speech, content_language=content, reading_context="cpp")
                self.assertNotIn("HIDDEN_REVIEW", " ".join(part["preparedText"] for part in reply["segments"]))

    def test_success_feedback_without_annotation_and_without_guidance_keeps_native_keys(self):
        for mathematical in (False, True):
            for guide in ("", "Texto válido."):
                with self.subTest(mathematical=mathematical, guide=guide):
                    block = dict(type="math_puzzle", schemaVersion=1, prompt="Observa.", responses=[], feedback=dict(correct=guide)) if mathematical else dict(
                        type="multiple_choice", question="Elige.", options=["A"], explanation=guide)
                    prefix = "✓ Correcto" if mathematical else "✓ Correct"
                    full = prefix + (("\n" if mathematical else "\n\n")+guide if guide else "")
                    self.assert_native_regions(block, [], [(full, None)], ui="fr", content_language="es",
                        reading_context="auto" if mathematical else "cpp")

    def test_content_language_owner_rejects_invalid_values_and_uses_stable_layout_fallback(self):
        block = dict(type="math_puzzle", schemaVersion=1, prompt="Observa.", responses=[], feedback=dict(correct="Texto válido."))
        # The stable source locale is es even while UI and speech are French.
        self.assert_native_regions(block, [], [("✓ Correcto\nTexto válido.", None)], ui="fr", language="fr")
        for invalid in ("de", True, ""):
            with self.subTest(invalid=invalid), self.assertRaisesRegex(AssertionError, "Narration failed"):
                self.worker.ask("curriculum_plan", lessonJson=json.dumps(self.lesson(block, [])), page=0,
                    language="es", voice="ef_dora", uiLanguage="es", contentLanguage=invalid)


if __name__ == "__main__":
    unittest.main()
