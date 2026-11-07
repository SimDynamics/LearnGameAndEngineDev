"""Explicit source ranges guide the real Windows frontend without synthesis."""
from copy import deepcopy
import hashlib
import unittest

from test_worker import ROOT, Worker


def units(text):
    return len(text.encode("utf-16-le")) // 2


def descriptor(source, role="cpp", reading="auto", start=0, end=None):
    end = units(source) if end is None else end
    selected = source.encode("utf-16-le")[2 * start:2 * end].decode("utf-16-le")
    return {"spans": [{"start": start, "end": end, "text": selected, "role": role, "reading": reading}]}


def field(owner, path, source, semantics):
    return dict(block=owner, path=path, sourceSha256=hashlib.sha256(source.encode()).hexdigest(), spans=semantics["spans"])


class ExplicitTextSemanticsTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.worker = Worker(missing_resources=True)

    @classmethod
    def tearDownClass(cls):
        cls.worker.close()

    def plan(self, source, semantics=None, **extra):
        request = dict(command="plan", text=source, language="es", readingContext="cpp", mathMode="auto")
        if semantics is not None:
            request["textSemantics"] = semantics
        return self.worker.ask(**dict(request, **extra))["segments"]

    def test_absence_is_the_exact_legacy_path_and_empty_descriptor_is_authoritative_prose(self):
        source = "La letra y se conserva."
        before = self.plan(source)
        self.assertEqual(before, self.plan(source))
        explicit = self.plan(source, {"spans": []})
        self.assertTrue(explicit)
        self.assertNotIn("griega", " ".join(part["spokenText"] for part in explicit))
        self.assertEqual(before, self.plan(source))

    def test_role_and_reading_apply_only_to_the_exact_selected_fragment(self):
        source = "letras y palabras: y."
        start = units(source[:source.rindex("y")])
        technical = self.plan(source, descriptor(source, start=start, end=start + 1))
        text = " ".join(part["spokenText"] for part in technical)
        self.assertIn("letras y palabras", text)
        self.assertEqual(1, text.count("griega"))
        natural = self.plan(source, descriptor(source, reading="natural", start=start, end=start + 1))
        self.assertNotIn("griega", " ".join(part["spokenText"] for part in natural))
        named = self.plan("7:3", descriptor("7:3", role="output", reading="technical"))
        self.assertIn("dos puntos", " ".join(part["spokenText"] for part in named))
        output = self.plan("7:3", descriptor("7:3", role="output"))
        self.assertNotIn("dos puntos", " ".join(part["spokenText"] for part in output))

    def test_adjacent_technical_ranges_are_one_lexical_input_with_original_owners(self):
        for source, first in (("x+y", 0), ("😀\r\n`x+y`.", units("😀\r\n`"))):
            with self.subTest(source=source):
                whole = descriptor(source, start=first, end=first + 3)
                divided = {"spans": [descriptor(source, start=first + at, end=first + at + 1)["spans"][0]
                                     for at in range(3)]}
                expected = self.plan(source, whole)
                self.assertEqual(expected, self.plan(source, divided))
                spoken = " ".join(part["spokenText"] for part in expected)
                self.assertIn("equis más i griega", spoken)
                raw = source.encode("utf-16-le")
                for part in expected:
                    self.assertEqual(part["text"], raw[2 * part["sourceStart"]:2 * part["sourceEnd"]].decode("utf-16-le"))
                    for shown in part["displayedRanges"]:
                        # Decoding independently also rejects a range through
                        # either half of the original supplementary character.
                        selected = part["text"].encode("utf-16-le")[2 * shown["start"]:2 * shown["end"]]
                        self.assertTrue(selected.decode("utf-16-le"))
        # Reading intent, rather than ink role, owns the lexical preparation.
        mixed = {"spans": [dict(start=0, end=1, text="x", role="cpp"),
                             dict(start=1, end=2, text="+", role="output", reading="technical"),
                             dict(start=2, end=3, text="y", role="string")]}
        self.assertEqual(self.plan("x+y", descriptor("x+y")), self.plan("x+y", mixed))

    def test_unmarked_whitespace_preserves_technical_context_without_crossing_natural_spans(self):
        for source, positions in (("x * y", (0, 2, 4)), ("x\r\n*\r\ny", (0, 3, 6))):
            with self.subTest(source=source):
                divided = {"spans": [descriptor(source, start=at, end=at + 1)["spans"][0] for at in positions]}
                expected = self.plan(source, descriptor(source))
                self.assertEqual(expected, self.plan(source, divided))
                self.assertIn("equis por i griega", " ".join(part["spokenText"] for part in expected))
        source = "x * y"
        # Explicit natural whitespace is an authored reading boundary; only
        # unmarked whitespace gaps may carry the surrounding technical context.
        explicit = {"spans": [descriptor(source, role="prose" if at in (1, 3) else "cpp",
                                                  reading="natural" if at in (1, 3) else "auto",
                                                  start=at, end=at + 1)["spans"][0] for at in range(5)]}
        self.assertIn("asterisco", " ".join(part["spokenText"] for part in self.plan(source, explicit)))
        source = "x y y"
        marked = {"spans": [descriptor(source, start=at, end=at + 1)["spans"][0] for at in (0, 4)]}
        self.assertIn("equis y i griega", " ".join(part["spokenText"] for part in self.plan(source, marked)))

    def test_literal_presentation_symbols_are_not_invented_markdown_delimiters(self):
        for source in ("izquierda**derecha", "**y**", "a`b", "a``b", "`  `",
                       "`\\(x+y\\)`", "`$x+y$`"):
            with self.subTest(source=source):
                prepared = " ".join(part["preparedText"] for part in self.plan(source, {"spans": []}))
                self.assertIn(source, prepared)
        # A literal unmatched delimiter in technical source is also retained.
        technical = " ".join(part["preparedText"] for part in self.plan("a`b", descriptor("a`b")))
        self.assertIn("`", technical)

    def test_complete_inline_wrappers_are_hidden_before_semantic_ranges_are_divided(self):
        for source, first, last in (("`y`", 1, 2), ("```y```", 3, 4), ("`x+y`", 1, 4)):
            with self.subTest(source=source):
                whole = self.plan(source, descriptor(source))
                inner = self.plan(source, descriptor(source, start=first, end=last))
                self.assertEqual(inner, whole)
                self.assertNotIn("`", " ".join(part["preparedText"] for part in whole))
        # Native inline formatting accepts URLs inside complete backticks;
        # protecting the URL itself must not protect its outer delimiters.
        url = "`https://ejemplo.test/y`"
        self.assertNotIn("`", " ".join(part["preparedText"] for part in self.plan(url, {"spans": []})))

    def test_mixed_semantic_boundaries_separate_introduced_words_only(self):
        for source, first, expected in (("ay", 1, "a i griega"), ("ya", 0, "i griega a")):
            with self.subTest(source=source):
                parts = self.plan(source, descriptor(source, start=first, end=first + 1))
                self.assertIn(expected, " ".join(part["spokenText"] for part in parts))
                self.assertEqual(source, " ".join(part["spokenText"] for part in self.plan(source, {"spans": []})))
        source = "`y`."
        parts = self.plan(source, descriptor(source, start=1, end=2))
        self.assertIn("i griega.", " ".join(part["spokenText"] for part in parts))

    def test_duplicate_strings_keep_distinct_field_owners_and_match_foreground_cache_keys(self):
        block = dict(type="multiple_choice", question="y", options=["y", "a"], explanation="y",
                     validator={"expected": "HIDDEN_ANSWER"}, reviewFeedback=[{"text": "HIDDEN_REVIEW"}])
        owner = "stable-choice"
        definitions = [("/question", "y", descriptor("y")),
                       ("/options/0", "y", descriptor("y", role="output")),
                       ("/explanation", "y", descriptor("y", role="string"))]
        lesson = dict(id="semantic-test", pages=[dict(blocks=[block])],
                      stateLayout={"version": 2, "current": [{"id": "page", "blocks": [owner]}]},
                      textSemantics={"version": 1, "fields": [field(owner, path, source, value) for path, source, value in definitions]})
        reply = self.worker.ask(command="curriculum_plan", lessonJson=__import__("json").dumps(lesson),
                                    page=0, language="es", voice="ef_dora", uiLanguage="es", readingContext="cpp")
        keys = [part["key"] for part in reply["segments"]]
        for path, source, value in definitions:
            for part in self.plan(source, value):
                self.assertIn(part["key"], keys, path)
        prepared = " ".join(part["preparedText"] for part in reply["segments"])
        self.assertNotIn("HIDDEN_ANSWER", prepared)
        self.assertNotIn("HIDDEN_REVIEW", prepared)
        self.assertNotEqual(self.plan("y", descriptor("y"))[0]["key"],
                            self.plan("y", descriptor("y", role="output"))[0]["key"])
        # Request-local scopes must never contaminate the next unmarked plan.
        self.assertEqual(self.plan("y"), self.plan("y"))

    def test_unicode_crlf_whitespace_and_source_word_owners_remain_original(self):
        source = "  😀\r\nletras y palabras: y  "
        start = units(source[:source.rindex("y")])
        parts = self.plan(source, descriptor(source, start=start, end=start + 1))
        raw = source.encode("utf-16-le")
        for part in parts:
            self.assertEqual(part["text"], raw[2 * part["sourceStart"]:2 * part["sourceEnd"]].decode("utf-16-le"))
            self.assertEqual(len(part["spokenRanges"]), len(part["displayedRanges"]))
            for shown in part["displayedRanges"]:
                self.assertLess(shown["start"], shown["end"])
                self.assertLessEqual(shown["end"], units(part["text"]))

    def test_independent_table_fields_keep_their_own_roles_and_allow_partial_annotation(self):
        import json
        owner = "stable-table"
        block = dict(type="math_puzzle", representations=[dict(type="math_table", config=dict(headers=["y", "y"], rows=[["y", "y"]]))], responses=[])
        paths = ["/representations/0/config/headers/0", "/representations/0/config/headers/1",
                 "/representations/0/config/rows/0/0", "/representations/0/config/rows/0/1"]
        fields = [field(owner, path, "y", descriptor("y", role="cpp" if index % 2 == 0 else "output"))
                  for index, path in enumerate(paths)]
        lesson = dict(id="semantic-table", pages=[dict(blocks=[block])],
                      stateLayout={"version": 2, "current": [{"id": "page", "blocks": [owner]}]},
                      textSemantics={"version": 1, "fields": fields})
        def background(value):
            return self.worker.ask("curriculum_plan", lessonJson=json.dumps(value), page=0, language="es", voice="ef_dora", uiLanguage="es", readingContext="cpp")
        keys = [part["key"] for part in background(lesson)["segments"]]
        for role in ("cpp", "output"):
            for part in self.plan("y", descriptor("y", role=role)):
                self.assertIn(part["key"], keys)
        partial = deepcopy(lesson)
        partial["textSemantics"]["fields"].pop(1)
        partial_keys = [part["key"] for part in background(partial)["segments"]]
        for part in self.worker.ask("plan", text="y", language="es", readingContext="auto", mathMode="auto")["segments"]:
            self.assertIn(part["key"], partial_keys)
        self.assertEqual(keys, [part["key"] for part in background(lesson)["segments"]])

    def test_hidden_unsupported_and_stale_curricular_pointers_are_rejected(self):
        import json
        owner = "visible-choice"
        block = dict(type="multiple_choice", question="Visible", options=["A", "B"],
                     explanation="Feedback", validator={"expected": "hidden"}, initialCode="hidden", id="hidden")
        lesson = dict(id="semantic-guard", pages=[dict(blocks=[block])],
                      stateLayout={"version": 2, "current": [{"id": "page", "blocks": [owner]}]})
        for path, source in (("/validator/expected", "hidden"), ("/initialCode", "hidden"),
                             ("/id", "hidden"), ("/question/anything", "Visible")):
            bad = deepcopy(lesson)
            bad["textSemantics"] = {"version": 1, "fields": [field(owner,path,source,{"spans": []})]}
            with self.subTest(path=path), self.assertRaisesRegex(AssertionError,"Narration failed"):
                self.worker.ask("curriculum_plan", lessonJson=json.dumps(bad), page=0, language="es", voice="ef_dora", uiLanguage="es", readingContext="cpp")
        stale = deepcopy(lesson)
        stale["textSemantics"] = {"version": 1, "fields": [field(owner,"/question","Visible",{"spans": []})]}
        stale["textSemantics"]["fields"][0]["sourceSha256"] = "0" * 64
        with self.assertRaisesRegex(AssertionError,"Narration failed"):
            self.worker.ask("curriculum_plan", lessonJson=json.dumps(stale), page=0, language="es", voice="ef_dora", uiLanguage="es", readingContext="cpp")

    def test_empty_card_winner_is_not_a_public_field_and_standalone_response_label_is_not_feedback(self):
        import json
        owner = "semantic-edge"
        def lesson(block, fields):
            return dict(id="semantic-edge", pages=[dict(blocks=[block])],
                        stateLayout={"version": 2, "current": [{"id": "page", "blocks": [owner]}]},
                        textSemantics={"version": 1, "fields": fields})
        def background(value):
            return self.worker.ask("curriculum_plan", lessonJson=json.dumps(value), page=0, language="es", voice="ef_dora", uiLanguage="es", readingContext="cpp")
        empty_card = dict(type="math_puzzle", responses=[dict(type="math_multiple_choice_single", options=[dict(id="fallback", label="")])])
        with self.assertRaisesRegex(AssertionError,"Narration failed"):
            background(lesson(empty_card,[field(owner,"/responses/0/options/0/label","",{"spans": []})]))
        # The standalone model's actual response identity is the native word
        # "response". Its same-named label is not part of feedback composition.
        standalone = dict(type="math_numeric_input", label="response", feedback={"correct": "y"})
        value = lesson(standalone,[field(owner,"/feedback/correct","y",descriptor("y", role="output"))])
        keys = [part["key"] for part in background(value)["segments"]]
        prefix = "✓ Correcto\n"
        source = prefix + "y"
        composed = descriptor(source, role="output", start=units(prefix), end=units(source))
        self.assertIn(self.plan(source,composed)[0]["key"],keys)

    def test_marked_compiler_guides_are_anticipated_with_exact_native_verdict_sources_only(self):
        import json
        owner = "stable-compiler"
        source = "\r\n  𐐀 y"
        selected = units(source[:source.rindex("y")])
        technical = descriptor(source, start=selected, end=selected + 1)
        natural = descriptor(source, role="output", start=selected, end=selected + 1)
        block = dict(type="cpp_compiler", initialCode="HIDDEN_INITIAL_CODE", initialStandardInput="HIDDEN_INITIAL_INPUT",
                     outputCheck={"cases": [dict(standardInput="HIDDEN_STDIN", outputs=["HIDDEN_ORACLE"],
                         explanation=source, reviewFeedback=[dict(text=source)]),
                         dict(standardInput="other", outputs=["HIDDEN_SECOND_ORACLE"], explanation=source)]})
        fields = [field(owner,"/outputCheck/cases/0/explanation",source,technical),
                  field(owner,"/outputCheck/cases/0/reviewFeedback/0/text",source,natural),
                  field(owner,"/outputCheck/cases/1/explanation",source,natural)]
        lesson = dict(id="semantic-compiler", pages=[dict(blocks=[block])],
                      stateLayout={"version": 2, "current": [{"id": "page", "blocks": [owner]}]})
        def background(value, ui="es"):
            return self.worker.ask("curriculum_plan", lessonJson=json.dumps(value), page=0, language="es", voice="ef_dora", uiLanguage=ui, readingContext="cpp")
        legacy = background(lesson)
        marked = deepcopy(lesson)
        marked["textSemantics"] = {"version": 1, "fields": fields}
        for ui in ("es", "en", "fr"):
            reply = background(marked,ui)
            keys = [part["key"] for part in reply["segments"]]
            for correct, value in ((True,technical), (False,natural), (True,natural)):
                body = source.lstrip("\r\n")
                removed = units(source) - units(body)
                word = ("Correcto" if correct else "Incorrecto") if ui == "es" else ("Correct" if correct else "Incorrect")
                prefix = ("✓ " if correct else "✗ ") + word + "\n\n"
                full = prefix + body
                spans = [dict(span, start=units(prefix) + span["start"] - removed,
                              end=units(prefix) + span["end"] - removed) for span in value["spans"]]
                foreground = self.plan(full, {"spans": spans}, readingContext="auto")
                for part in foreground:
                    self.assertIn(part["key"], keys, (ui,correct,value))
            emitted = " ".join(part["preparedText"] for part in reply["segments"])
            for hidden in ("HIDDEN_INITIAL_CODE", "HIDDEN_INITIAL_INPUT", "HIDDEN_STDIN", "HIDDEN_ORACLE", "HIDDEN_SECOND_ORACLE"):
                self.assertNotIn(hidden,emitted)
        self.assertEqual(legacy["segments"],background(lesson)["segments"])
        empty_overlay = deepcopy(lesson)
        empty_overlay["textSemantics"] = {"version": 1, "fields": []}
        self.assertEqual(legacy["segments"],background(empty_overlay)["segments"])
        # Compilation rejection guides use the same explicit-only rule, without
        # requiring a runtime output oracle or accessing editable source.
        compile_lesson = deepcopy(lesson)
        compile_lesson["pages"][0]["blocks"][0] = dict(type="cpp_compiler", initialCode="HIDDEN_COMPILE_SOURCE",
                                                       compileCheck={"diagnostic": "prepared-diagnostic", "explanation": source})
        compile_lesson["textSemantics"] = {"version": 1, "fields": [field(owner,"/compileCheck/explanation",source,technical)]}
        compile_reply = background(compile_lesson)
        self.assertTrue(any("griega" in part["preparedText"] for part in compile_reply["segments"]))
        self.assertNotIn("HIDDEN_COMPILE_SOURCE", " ".join(part["preparedText"] for part in compile_reply["segments"]))

    def test_current_l25_l26_compiler_guides_keep_native_prepared_identity(self):
        import json
        base = ROOT / "app/src/main/assets/content/es/programming/cpp_fundamentals/archivos_y_programa_completo"
        inventory = {
            "lesson_comprobar_ida_y_vuelta_sin_perder_datos.json": {
                "block_0002_0003", "block_0006_0002"},
            "lesson_construir_un_programa_de_carga_y_guardado.json": {
                "block_0002_0003", "block_0004_0008", "block_0004_0012",
                "block_0005_0007", "block_0007_0007"},
        }
        empty_success = {"block_0004_0012", "block_0005_0007", "block_0007_0007"}
        checked = set()
        for filename, owners in inventory.items():
            asset = base / filename
            original = asset.read_bytes()
            lesson = json.loads(original.decode("utf-8-sig"))
            snapshot = deepcopy(lesson)
            fields = {(item["block"], item["path"]): item
                      for item in lesson["textSemantics"]["fields"]}
            selected = {}
            for page_index, page in enumerate(lesson["pages"]):
                for block_index, block in enumerate(page["blocks"]):
                    owner = lesson["stateLayout"]["current"][page_index]["blocks"][block_index]
                    if owner in owners:
                        self.assertNotIn(owner, selected)
                        selected[owner] = (page_index, block)
            self.assertEqual(owners, set(selected), filename)
            background = {}
            for owner in sorted(owners):
                page_index, block = selected[owner]
                self.assertEqual("cpp_compiler", block["type"])
                self.assertEqual(1, len(block["outputCheck"]["cases"]))
                case = block["outputCheck"]["cases"][0]
                self.assertEqual(0, case.get("exitCode", 0))
                self.assertEqual(1, len(case["reviewFeedback"]))
                guides = ((True, "/outputCheck/cases/0/explanation", case["explanation"]),
                          (False, "/outputCheck/cases/0/reviewFeedback/0/text", case["reviewFeedback"][0]["text"]))
                for correct, path, source in guides:
                    identity = (filename, owner, path)
                    checked.add(identity)
                    authored = fields[(owner, path)]
                    self.assertEqual(hashlib.sha256(source.encode("utf-8")).hexdigest(), authored["sourceSha256"], identity)
                    self.assertEqual(correct and owner in empty_success, not authored["spans"], identity)
                    raw = source.encode("utf-16-le")
                    for span in authored["spans"]:
                        self.assertEqual(span["text"], raw[2 * span["start"]:2 * span["end"]].decode("utf-16-le"), identity)
                    # Compiler feedback is a whole native RichEdit region. Its
                    # verdict follows UI language, while the authored Spanish
                    # body and its roles/readings remain exact and independent.
                    body = source.lstrip("\r\n")
                    removed = units(source) - units(body)
                    for ui in ("es", "en", "fr"):
                        with self.subTest(asset=filename, owner=owner, path=path, ui=ui):
                            word = ("Correcto" if correct else "Incorrecto") if ui == "es" else ("Correct" if correct else "Incorrect")
                            prefix = ("✓ " if correct else "✗ ") + word + ("\n\n" if body else "")
                            full = prefix + body
                            spans = []
                            for span in authored["spans"]:
                                first = max(span["start"], removed)
                                if first >= span["end"]:
                                    continue
                                shifted = dict(span, start=units(prefix) + first - removed,
                                               end=units(prefix) + span["end"] - removed,
                                               text=raw[2 * first:2 * span["end"]].decode("utf-16-le"))
                                self.assertEqual(span["role"], shifted["role"])
                                self.assertEqual(span.get("reading", "auto"), shifted.get("reading", "auto"))
                                spans.append(shifted)
                            immediate = self.plan(full, {"spans": spans}, voice="ef_dora", readingContext="auto")
                            self.assertTrue(immediate)
                            if not authored["spans"]:
                                # Explicit [] owns all prose; an automatically
                                # inferred C++ operator is never its oracle.
                                natural = descriptor(full, role="prose", reading="natural")
                                self.assertEqual(immediate, self.plan(full, natural, voice="ef_dora", readingContext="auto"))
                            cache = (page_index, ui)
                            if cache not in background:
                                background[cache] = self.worker.ask("curriculum_plan", lessonJson=original.decode("utf-8-sig"),
                                    page=page_index, language="es", voice="ef_dora", uiLanguage=ui,
                                    contentLanguage="es", readingContext="cpp")["segments"]
                            full_bytes = full.encode("utf-16-le")
                            for part in immediate:
                                self.assertEqual(part["text"], full_bytes[2 * part["sourceStart"]:2 * part["sourceEnd"]].decode("utf-16-le"))
                                self.assertEqual(len(part["spokenRanges"]), len(part["displayedRanges"]))
                                for spoken, shown in zip(part["spokenRanges"], part["displayedRanges"]):
                                    self.assertTrue(0 <= spoken["start"] < spoken["end"] <= units(part["spokenText"]))
                                    self.assertTrue(0 <= shown["start"] < shown["end"] <= units(part["text"]))
                                    part["text"].encode("utf-16-le")[2 * shown["start"]:2 * shown["end"]].decode("utf-16-le")
                                # Anticipation exports normalized speech and
                                # cache identity, not the reader's source map.
                                # Check that map above against the raw field;
                                # compare every shared transport value here.
                                expected = dict(text=part["spokenText"], preparedText=part["preparedText"], key=part["key"])
                                self.assertIn(expected, background[cache])
            self.assertEqual(snapshot, lesson)
            self.assertEqual(original, asset.read_bytes())
        self.assertEqual(14, len(checked))

    def test_malformed_ranges_roles_presence_and_exact_text_are_rejected(self):
        source = "😀 y"
        valid = descriptor(source, start=3, end=4)
        variants = []
        for patch in ({"start": 1}, {"end": 5}, {"text": "x"}, {"role": "unknown"},
                      {"reading": "phonetic"}, {"start": True}, {"end": 4.0}):
            wrong = deepcopy(valid)
            wrong["spans"][0].update(patch)
            variants.append(wrong)
        overlap = deepcopy(valid)
        overlap["spans"].append(deepcopy(overlap["spans"][0]))
        variants += [overlap, {"spans": False}, {"spans": [], "phonemes": "forbidden"}]
        for value in variants:
            with self.subTest(value=value), self.assertRaisesRegex(AssertionError,"Narration failed"):
                self.plan(source, value)


if __name__ == "__main__":
    unittest.main()
