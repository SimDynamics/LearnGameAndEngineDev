"""Anticipation must use each source actually owned by a native reading region."""
from copy import deepcopy
from importlib import util
import json
from pathlib import Path
import unittest

from test_text_semantics import descriptor, field
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

    def assert_native_regions(self, block, definitions, regions):
        lesson = self.lesson(block, definitions)
        snapshot = deepcopy(lesson)
        reply = self.worker.ask("curriculum_plan", lessonJson=json.dumps(lesson), page=0,
                                language="es", voice="ef_dora", uiLanguage="es", readingContext="cpp")
        self.assertEqual(snapshot, lesson)
        keys = {item["key"] for item in reply["segments"]}
        for source, semantics in regions:
            request = dict(text=source, language="es", voice="ef_dora", readingContext="cpp", mathMode="auto")
            if semantics is not None:
                request["textSemantics"] = semantics
            expected = self.worker.ask("plan", **request)["segments"]
            for item in expected:
                self.assertIn(item["key"], keys, dict(nativeSource=source, expected=item["preparedText"],
                    anticipated=[part["preparedText"] for part in reply["segments"]]))

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


if __name__ == "__main__":
    unittest.main()
