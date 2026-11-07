"""Headless contract tests. No authored asset or learner state is opened for writing."""
from copy import deepcopy
import hashlib
from importlib import util
import json
from pathlib import Path
import unittest

ROOT = Path(__file__).parent
spec = util.spec_from_file_location("semantic_state_layout_test", ROOT / "stable_state_layout.py")
layout = util.module_from_spec(spec)
spec.loader.exec_module(layout)
semantics = layout.text_semantics


def lesson():
    source = {"id": "qa_semantics", "title": "QA", "pages": [{"blocks": [
        {"type": "paragraph", "text": "😀 Usa `int p;`. Salida: N=1a|1b. (Autor, 2024, p. 9)",
         "emphasis": [{"text": "Usa", "style": "bold"}]},
        {"type": "multiple_choice", "id": "choice", "question": "Elige N=1.",
         "options": ["N=1", "N=2"], "correctIndex": 0, "explanation": "Resultado: N=1.",
         "reviewFeedback": ["Bien: N=1", "Revisa: N=2"]},
        {"type": "math_puzzle", "schemaVersion": 1, "id": "nested", "prompt": "Relaciona N=1.",
         "representations": [{"type": "math_table", "headers": ["Salida"], "rows": [["N=1"]]}],
         "responses": [{"id": "pairs", "type": "math_matching_drag",
             "sources": [{"id": "a", "label": "a", "content": [{"type": "rich_answer_option", "content": [
                 {"type": "text", "text": "N=1"}]}]}],
             "targets": [{"id": "b", "label": "N=1"}], "validator": {"type": "matching", "expected": {"a": "b"}}}],
         "feedback": {"correct": "Correcto: N=1.", "incorrect": "Comprueba N=1."}},
        {"type": "cpp_compiler", "initialCode": "int main() {}", "outputCheck": {"cases": [
            {"standardInput": "", "outputs": ["N=1"], "explanation": "Salida: N=1.",
             "reviewFeedback": [{"outputs": ["N=2"], "text": "Revisa N=2."}]}]}}
    ]}]}
    return layout.prepare_v2(source, "es", {"es": source})


def annotate(document, block, path, fragment=None, role="output", reading="auto"):
    owner = document["stateLayout"]["current"][0]["blocks"][block]
    source = semantics.public_text_fields(document["pages"][0]["blocks"][block])[path]
    spans = [] if fragment is None else [semantics.select(source, fragment, role, reading=reading)]
    field = semantics.make_field(document, owner, path, spans)
    document.setdefault("textSemantics", {"version": 1, "fields": []})["fields"].append(field)
    return field


class TextSemanticsTests(unittest.TestCase):
    def test_preserves_every_response_source_and_state_contract(self):
        original = lesson()
        candidate = deepcopy(original)
        annotate(candidate, 0, "/text", "int p;", "cpp", "technical")
        annotate(candidate, 1, "/options/1", "N=2")
        annotate(candidate, 1, "/reviewFeedback/1", "N=2")
        annotate(candidate, 2, "/representations/0/rows/0/0", "N=1")
        annotate(candidate, 2, "/responses/0/sources/0/content/0/content/0/text", "N=1")
        annotate(candidate, 2, "/feedback/incorrect", "N=1")
        annotate(candidate, 2, "/feedback/correct")
        annotate(candidate, 3, "/outputCheck/cases/0/explanation", "N=1")
        annotate(candidate, 3, "/outputCheck/cases/0/reviewFeedback/0/text", "N=2")
        layout.validate(candidate)
        layout.validate_transition(original, candidate)
        self.assertEqual(original["pages"], candidate["pages"])
        self.assertEqual(original["stateLayout"], candidate["stateLayout"])
        self.assertEqual([layout.state_fingerprint(b) for b in original["pages"][0]["blocks"]],
                         [layout.state_fingerprint(b) for b in candidate["pages"][0]["blocks"]])
        self.assertEqual(json.dumps(original["pages"][0]["blocks"][2], ensure_ascii=False),
                         json.dumps(candidate["pages"][0]["blocks"][2], ensure_ascii=False))
        owner = candidate["stateLayout"]["current"][0]["blocks"][0]
        resolved = semantics.validate(candidate)[owner]["/text"]
        self.assertEqual(resolved["source"], original["pages"][0]["blocks"][0]["text"])
        self.assertEqual(resolved["spans"][0]["start"], 8)  # supplementary emoji occupies two units

    def test_empty_field_is_explicit_prose_and_legacy_absence_is_noop(self):
        document = lesson()
        self.assertEqual(semantics.validate(document), {})
        annotate(document, 0, "/text")
        descriptor = next(iter(semantics.validate(document).values()))["/text"]
        self.assertEqual(descriptor["spans"], [])
        layout.validate(document)

    def test_rejects_stale_hidden_duplicate_and_noncanonical_targets(self):
        original = lesson()
        annotate(original, 0, "/text", "int p;", "cpp")
        for change in (
            lambda d: d["textSemantics"]["fields"][0].update(sourceSha256="0" * 64),
            lambda d: d["textSemantics"]["fields"][0].update(block="unknown"),
            lambda d: d["textSemantics"]["fields"][0].update(path="/text/"),
            lambda d: d["textSemantics"]["fields"].append(deepcopy(d["textSemantics"]["fields"][0])),
            lambda d: d["textSemantics"].update(version=True),
            lambda d: d["textSemantics"].update(version=1.0),
            lambda d: d["textSemantics"].update(extra=True),
            lambda d: d.pop("stateLayout"),
        ):
            bad = deepcopy(original)
            change(bad)
            with self.assertRaises(ValueError):
                semantics.validate(bad)
        for block, path in ((1, "/correctIndex"), (2, "/responses/0/validator/expected/a"),
                            (3, "/initialCode"), (3, "/outputCheck/cases/0/outputs/0"),
                            (3, "/outputCheck/cases/0/standardInput"), (2, "/responses/00/targets/0/label")):
            with self.subTest(path=path), self.assertRaises(ValueError):
                semantics.make_field(original, original["stateLayout"]["current"][0]["blocks"][block], path, [])

    def test_rejects_bad_ranges_types_intents_and_split_surrogates(self):
        original = lesson()
        annotate(original, 0, "/text", "int p;", "cpp")
        mutations = [dict(start=True), dict(start=9.0), dict(start=-1), dict(end=99999),
                     dict(end=9), dict(text="int q;"), dict(role="keyword"), dict(role=[]),
                     dict(reading="phonetic"), dict(reading=[]), dict(background="red")]
        for values in mutations:
            bad = deepcopy(original)
            bad["textSemantics"]["fields"][0]["spans"][0].update(values)
            with self.subTest(values=values), self.assertRaises(ValueError):
                semantics.validate(bad)
        for span in ({"start": 0, "end": 1, "text": "😀", "role": "prose"},
                     {"start": 1, "end": 2, "text": "😀", "role": "prose"}):
            bad = deepcopy(original)
            bad["textSemantics"]["fields"][0]["spans"] = [span]
            with self.assertRaises(ValueError):
                semantics.validate(bad)
        bad = deepcopy(original)
        spans = bad["textSemantics"]["fields"][0]["spans"]
        spans.append(deepcopy(spans[0]))
        with self.assertRaises(ValueError):
            semantics.validate(bad)

    def test_helper_selects_occurrence_and_keeps_adjacent_punctuation_outside(self):
        source = '😀 `p;`. p;\r\n(Autor, 2024, p. 9)'
        with self.assertRaises(ValueError):
            semantics.select(source, "p;", "cpp")
        first = semantics.select(source, "p;", "cpp", occurrence=1)
        second = semantics.select(source, "p;", "data", occurrence=2)
        self.assertEqual((first["start"], first["end"]), (4, 6))
        self.assertEqual(source.encode("utf-16-le")[second["start"]*2:second["end"]*2].decode("utf-16-le"), "p;")
        self.assertNotIn("reading", first)

    def test_characters_is_an_exact_intent_independent_of_the_existing_role(self):
        for role in semantics.ROLES:
            for reading in ("auto", "natural", "technical", "characters"):
                with self.subTest(role=role, reading=reading):
                    document = lesson()
                    source = document["pages"][0]["blocks"][0]["text"]
                    span = semantics.select(source, "😀", role, reading=reading)
                    self.assertEqual((span["start"], span["end"]), (0, 2))
                    self.assertEqual(span["role"], role)
                    if reading == "auto":
                        self.assertNotIn("reading", span)
                    else:
                        self.assertEqual(span["reading"], reading)
                    owner = document["stateLayout"]["current"][0]["blocks"][0]
                    document["textSemantics"] = dict(version=1, fields=[semantics.make_field(document, owner, "/text", [span])])
                    resolved = semantics.validate(document)[owner]["/text"]["spans"][0]
                    self.assertEqual(resolved, span)

    def test_characters_authoring_on_the_current_character_option_preserves_curriculum_and_state(self):
        path = ROOT.parents[1] / "app/src/main/assets/content/es/programming/cpp_fundamentals/archivos_y_programa_completo/lesson_clasificar_caracteres_y_normalizar_una_copia.json"
        raw = path.read_bytes()
        original = json.loads(raw.decode("utf-8-sig"))
        owner, pointer = "block_0007_0004", "/options/0"
        field_index = next(i for i, item in enumerate(original["textSemantics"]["fields"]) if item["block"] == owner and item["path"] == pointer)
        source_field = original["textSemantics"]["fields"][field_index]
        index = next(i for i, span in enumerate(source_field["spans"]) if span["text"] == "!" and span["role"] == "data")
        for reading in sorted(semantics.READINGS):
            candidate = deepcopy(original)
            candidate["textSemantics"]["fields"][field_index]["spans"][index]["reading"] = reading
            expected = deepcopy(source_field["spans"])
            expected[index]["reading"] = reading
            with self.subTest(reading=reading):
                resolved = semantics.validate(candidate)[owner][pointer]
                self.assertEqual(resolved["spans"], expected)
                layout.validate(candidate)
                layout.validate_transition(original, candidate)
                self.assertEqual(candidate["pages"], original["pages"])
                self.assertEqual(candidate["stateLayout"], original["stateLayout"])
        self.assertEqual(path.read_bytes(), raw)

    def test_character_intent_rejects_unknown_spellings_and_non_string_values(self):
        for reading in ("character", "Characters", "characters ", "phonetic", "", None, True, 1, [], {}):
            document = lesson()
            field = annotate(document, 0, "/text", "int p;", "cpp")
            field["spans"][0]["reading"] = reading
            with self.subTest(reading=reading), self.assertRaises(ValueError):
                semantics.validate(document)

    def test_rejects_unsupported_canvas_and_dropdown_but_accepts_fixed_table_fields(self):
        block = {"type": "math_puzzle", "prompt": "Completa", "representations": [
            {"type": "geometry_2d_diagram", "label": "Figura"}], "responses": [
            {"type": "math_multi_input", "layout": "table", "headers": ["Dato"], "rows": [[{"field": "x"}, "N=1"]],
             "fields": [{"id": "x", "type": "fixed", "text": "N=1", "label": "Dato", "unit": "m"},
                        {"id": "y", "type": "dropdown", "options": [{"id": "a", "label": "Oculto"}]}]}]}
        paths = semantics.public_text_fields(block)
        for path in ("/responses/0/headers/0", "/responses/0/rows/0/1", "/responses/0/fields/0/text"):
            self.assertIn(path, paths)
        for path in ("/representations/0/label", "/responses/0/fields/1/options/0/label"):
            self.assertNotIn(path, paths)

    def test_feedback_group_requires_all_real_origins_and_forbids_unused_response_feedback(self):
        document = lesson()
        annotate(document, 2, "/feedback/correct", "N=1")
        with self.assertRaises(ValueError):
            semantics.validate(document)
        annotate(document, 2, "/feedback/incorrect")
        semantics.validate(document)
        block = {"type": "math_puzzle", "prompt": "Elige", "responses": [{"id": "r", "label": "Respuesta", "type": "math_multiple_choice_single",
            "options": [{"id": "a", "label": "A", "reviewFeedback": "Revisa N=1"}],
            "feedback": {"correct": "Nunca usado"}}]}
        paths = semantics.public_text_fields(block)
        self.assertIn("/responses/0/options/0/reviewFeedback", paths)
        self.assertNotIn("/responses/0/feedback/correct", paths)
        self.assertEqual(semantics.feedback_origins(block), {"/responses/0/label", "/responses/0/options/0/reviewFeedback"})

    def test_native_regions_allow_independent_field_annotations_without_rewriting_state(self):
        block = {"type": "math_puzzle", "schemaVersion": 1, "id": "regions", "prompt": "Completa",
                 "representations": [{"type": "math_table", "headers": ["Salida", "Nombre"], "rows": [["N=1", "letras y palabras"]]}],
                 "responses": [{"id": "r", "type": "math_structured_answer", "fields": [
                     {"id": "x", "type": "fixed", "label": "Cantidad", "text": "N=1", "unit": "m"}],
                     "segments": [{"text": "Vale"}, {"field": "x"}, {"text": "fin"}], "validator": {"type": "fields", "fields": {}}}]}
        table = deepcopy(block)
        table["responses"][0].pop("segments")
        table["responses"][0].update(layout="table", headers=["Dato", "Unidad"], rows=[[{"field": "x"}, "otra celda"]])
        ordinary = deepcopy(block)
        ordinary["responses"][0].pop("segments")
        nested = deepcopy(block)
        nested["representations"] = [{"type": "rich_answer_option", "content": [block["representations"][0]]}]
        for variant in (block, table, ordinary, nested):
            raw = {"id": "qa_regions", "title": "QA", "pages": [{"blocks": [variant]}]}
            document = layout.prepare_v2(raw, "es", {"es": raw})
            paths = semantics.public_text_fields(variant)
            for selected, source in paths.items():
                if not source or selected == "/prompt":
                    continue
                with self.subTest(selected=selected, variant=variant):
                    candidate = deepcopy(document)
                    annotate(candidate, 0, selected, source)
                    resolved = semantics.validate(candidate)
                    owner = candidate["stateLayout"]["current"][0]["blocks"][0]
                    self.assertEqual(set(resolved[owner]), {selected})
                    self.assertEqual(resolved[owner][selected]["source"], source)
                    layout.validate(candidate)
                    layout.validate_transition(document, candidate)
                    self.assertEqual(document["pages"], candidate["pages"])
                    self.assertEqual(document["stateLayout"], candidate["stateLayout"])

    def test_independent_field_units_do_not_relax_shared_feedback_origins(self):
        block = {"type": "math_puzzle", "schemaVersion": 1, "prompt": "Completa",
                 "feedback": {"correct": "Bien", "incorrect": "Revisa"}, "responses": [
                     {"id": "r", "type": "math_multi_input", "fields": [
                         {"id": "x", "label": "Cantidad", "unit": "m"}], "validator": {"type": "fields", "fields": {}}}]}
        raw = {"id": "qa_feedback_regions", "title": "QA", "pages": [{"blocks": [block]}]}
        document = layout.prepare_v2(raw, "es", {"es": raw})
        candidate = deepcopy(document)
        annotate(candidate, 0, "/responses/0/fields/0/unit", "m")
        semantics.validate(candidate)
        annotate(candidate, 0, "/responses/0/fields/0/label", "Cantidad")
        with self.assertRaisesRegex(ValueError, "feedback"):
            semantics.validate(candidate)
        annotate(candidate, 0, "/feedback/correct")
        annotate(candidate, 0, "/feedback/incorrect")
        semantics.validate(candidate)

    def test_empty_card_fallback_is_not_public_and_standalone_label_has_its_real_owner(self):
        hidden = {"type": "math_puzzle", "prompt": "Elige", "responses": [{"id": "r", "type": "math_matching_drag",
            "sources": [{"id": "a", "label": "", "text": "Unused"}], "targets": []}]}
        self.assertNotIn("/responses/0/sources/0/label", semantics.public_text_fields(hidden))
        standalone = {"type": "math_numeric_input", "id": "legacy_id", "label": "legacy_id"}
        self.assertIn("/label", semantics.feedback_origins(standalone))


class LessonRootTextSemanticsTests(unittest.TestCase):
    @staticmethod
    def root_lesson():
        return {"id": "qa_root_semantics", "title": "😀 `int` y 2: !\r\ne\u0301", "pages": [{"blocks": []}]}

    @staticmethod
    def annotate_title(document, spans):
        field = semantics.make_root_field(document, "/title", spans)
        document.setdefault("textSemantics", {"version": 1, "fields": []})["rootFields"] = [field]
        return field

    def test_root_only_is_exact_without_layout_or_invented_block(self):
        original = self.root_lesson()
        candidate = deepcopy(original)
        spans = [semantics.select(candidate["title"], "int", "cpp", reading="technical"),
                 semantics.select(candidate["title"], "2", "data"),
                 semantics.select(candidate["title"], "!", "data", reading="characters")]
        field = self.annotate_title(candidate, spans)
        self.assertEqual(set(field), {"path", "sourceSha256", "spans"})
        self.assertEqual(field["sourceSha256"], hashlib.sha256(original["title"].encode("utf-8")).hexdigest())
        self.assertEqual(spans[0]["start"], 4)  # supplementary emoji + space + backtick
        self.assertEqual(semantics.validate(candidate), {})  # original block-owner API stays compatible
        self.assertEqual(semantics.validate_root_fields(candidate), {"/title": {"source": original["title"], "spans": spans}})
        layout.validate(candidate)
        self.assertNotIn("stateLayout", candidate)
        self.assertEqual({key: value for key, value in candidate.items() if key != "textSemantics"}, original)
        # Returned descriptors do not alias mutable authored spans.
        semantics.validate_root_fields(candidate)["/title"]["spans"].clear()
        self.assertEqual(field["spans"], spans)

    def test_explicit_prose_and_all_existing_roles_readings_are_independent(self):
        for role in sorted(semantics.ROLES):
            for reading in sorted(semantics.READINGS):
                document = self.root_lesson()
                span = semantics.select(document["title"], "!", role, reading=reading)
                self.annotate_title(document, [span])
                with self.subTest(role=role, reading=reading):
                    self.assertEqual(semantics.validate_root_fields(document)["/title"]["spans"], [span])
        document = self.root_lesson()
        self.annotate_title(document, [])
        self.assertEqual(semantics.validate_root_fields(document)["/title"]["spans"], [])
        self.assertEqual(semantics.validate_root_fields(self.root_lesson()), {})

    def test_preparation_and_transition_keep_family_ids_and_raw_blocks(self):
        original = self.root_lesson()
        candidate = deepcopy(original)
        self.annotate_title(candidate, [semantics.select(candidate["title"], "int", "cpp")])
        self.assertEqual(layout.prepare(original)["stateLayout"], layout.prepare(candidate)["stateLayout"])
        previous = layout.prepare_v2(original, "es", {"es": original})
        current = layout.prepare_v2(candidate, "es", {"es": original}, previous=original)
        self.assertEqual(current["stateLayout"], previous["stateLayout"])
        self.assertEqual(current["pages"], previous["pages"])
        layout.validate_transition(previous, current)
        mixed = lesson()
        before = deepcopy(mixed)
        self.annotate_title(mixed, [])
        annotate(mixed, 0, "/text", "int p;", "cpp")
        layout.validate_transition(before, mixed)
        self.assertEqual(layout.prepare_v2(mixed, "es", {"es": before}, previous=before)["stateLayout"], before["stateLayout"])
        self.assertEqual(mixed["pages"], before["pages"])
        self.assertEqual(mixed["stateLayout"], before["stateLayout"])
        # Root-only support does not bootstrap an owner for block annotations.
        del mixed["stateLayout"]
        with self.assertRaises(ValueError):
            semantics.validate(mixed)

    def test_root_fields_reject_stale_hidden_duplicate_and_wrong_schema(self):
        original = self.root_lesson()
        self.annotate_title(original, [semantics.select(original["title"], "int", "cpp")])
        mutations = [
            lambda d: d["textSemantics"].pop("fields"),
            lambda d: d["textSemantics"].update(rootFields={}),
            lambda d: d["textSemantics"]["rootFields"].append(deepcopy(d["textSemantics"]["rootFields"][0])),
            lambda d: d["textSemantics"]["rootFields"][0].update(block=""),
            lambda d: d["textSemantics"]["rootFields"][0].update(sourceSha256="0" * 64),
            lambda d: d.update(title=d["title"].replace("e\u0301", "é")),
            lambda d: d.update(title=7),
            lambda d: d["textSemantics"].update(version=True),
            lambda d: d["textSemantics"]["rootFields"][0].update(color="red"),
        ]
        mutations.extend(lambda d, path=path: d["textSemantics"]["rootFields"][0].update(path=path)
                         for path in ("/id", "/title/", "title", "/Title", "/pages/0/title", "/title~0", "/stateLayout"))
        for index, change in enumerate(mutations):
            candidate = deepcopy(original)
            change(candidate)
            with self.subTest(index=index), self.assertRaises(ValueError):
                semantics.validate(candidate)
        for path in ("/id", "/title/", "/pages/0/title"):
            with self.subTest(path=path), self.assertRaises(ValueError):
                semantics.make_root_field(original, path, [])

    def test_root_spans_reject_nonexact_utf16_ranges_and_unknown_intents(self):
        original = self.root_lesson()
        self.annotate_title(original, [semantics.select(original["title"], "int", "cpp")])
        mutations = [dict(start=True), dict(end=100000), dict(text="INT"), dict(reading="character"),
                     dict(role="keyword"), dict(background="gray"), dict(start=0, end=1, text="😀"),
                     dict(start=1, end=2, text="😀")]
        for values in mutations:
            candidate = deepcopy(original)
            candidate["textSemantics"]["rootFields"][0]["spans"][0].update(values)
            with self.subTest(values=values), self.assertRaises(ValueError):
                semantics.validate_root_fields(candidate)
        candidate = deepcopy(original)
        spans = candidate["textSemantics"]["rootFields"][0]["spans"]
        spans.append(deepcopy(spans[0]))
        with self.assertRaises(ValueError):
            semantics.validate(candidate)

    def test_image_inventory_exposes_only_consumed_alt_and_caption(self):
        image = {"type": "image", "imageId": "qa_image", "src": "images/qa_image.png", "alt": "N=1", "caption": "N=1"}
        self.assertEqual(semantics.public_text_fields(image), {"/alt": "N=1", "/caption": "N=1"})
        image.pop("caption")
        self.assertEqual(semantics.public_text_fields(image), {"/alt": "N=1"})


if __name__ == "__main__":
    unittest.main()
