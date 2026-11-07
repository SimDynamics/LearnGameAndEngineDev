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

    def test_unmarked_prose_never_infers_named_code_letters_and_old_plans_remain_replayable(self):
        import json
        sources = ("Caracteres y valores lógicos", "El carácter y cualquier emoji forman texto.",
                   "El carácter y también la palabra permanecen.", "La letra y es una consonante.",
                   "La letra b es una consonante.", "El valor a se conserva.",
                   "La variable y conserva el dato.", "La palabra final describe el cierre.")
        def recordings(plan):
            return [(part["spokenText"], part["preparedText"], part["key"]) for part in plan]
        baseline = {}
        for source in sources:
            for context in ("auto", "cpp"):
                with self.subTest(source=source, context=context):
                    plain = self.plan(source, voice="ef_dora", readingContext=context)
                    empty = self.plan(source, {"spans": []}, voice="ef_dora", readingContext=context)
                    self.assertEqual(source, " ".join(part["spokenText"] for part in plain))
                    self.assertEqual(recordings(plain), recordings(empty))
                    baseline[(source, context)] = plain
                    raw = source.encode("utf-16-le")
                    for part in plain + empty:
                        self.assertEqual(part["text"], raw[2 * part["sourceStart"]:2 * part["sourceEnd"]].decode("utf-16-le"))
                        self.assertEqual(len(part["spokenRanges"]), len(part["displayedRanges"]))
        # This is the exact result measured from the preceding real Worker,
        # before removing its ambiguous singular-noun/ASCII-letter inference.
        source = sources[1]
        old_spoken = "El carácter i griega cualquier emoji forman texto."
        previous = self.plan(source, voice="ef_dora", previousProseInference=True)
        self.assertEqual([old_spoken], [part["spokenText"] for part in previous])
        self.assertEqual(["\ue002" + old_spoken], [part["preparedText"] for part in previous])
        self.assertNotEqual([part["key"] for part in previous], [part["key"] for part in baseline[(source, "cpp")]])
        for policy in ("b42", "original"):
            self.assertEqual(previous, self.plan(source, voice="ef_dora", outputDataPolicy=policy))
            self.assertEqual(previous, self.plan(source, voice="ef_dora", outputDataPolicy=policy,
                                                previousProseInference=True))
        # The replay switch never defeats authoritative prose or explicit code.
        self.assertEqual(recordings(baseline[(source, "cpp")]), recordings(
                         self.plan(source, {"spans": []}, voice="ef_dora", previousProseInference=True)))
        explicit = (("La variable `y` contiene 1.", "La variable i griega contiene 1.", None),
                    ("`int y = 1;` conserva la variable.", "int i griega asigna uno punto y coma conserva la variable.", None),
                    ("La letra y es una consonante.", "La letra i griega es una consonante.",
                     descriptor(sources[3], start=units("La letra "), end=units("La letra y"))))
        for text, expected, semantics in explicit:
            with self.subTest(explicit=text):
                direct = self.plan(text, semantics, voice="ef_dora")
                self.assertEqual(expected, " ".join(part["spokenText"] for part in direct))
                self.assertEqual(direct, self.plan(text, semantics, voice="ef_dora", previousProseInference=True))
        # Request-local historical replay cannot contaminate later background
        # planning, including titles, body prose and explicitly empty overlays.
        blocks = [dict(type="paragraph", text=value) for value in sources]
        lesson = dict(id="qa-prose-inference", title=sources[0], pages=[dict(blocks=blocks)],
                      stateLayout=dict(version=2, current=[dict(id="page", blocks=[f"block-{n}" for n in range(len(blocks))])]))
        for annotated in (False, True):
            value = deepcopy(lesson)
            if annotated:
                value["textSemantics"] = dict(version=1, fields=[
                    field(f"block-{n}", "/text", text, {"spans": []}) for n, text in enumerate(sources)])
            snapshot = deepcopy(value)
            background = self.worker.ask("curriculum_plan", lessonJson=json.dumps(value), page=0, language="es",
                voice="ef_dora", uiLanguage="es", contentLanguage="es", readingContext="cpp")["segments"]
            for text in sources:
                for part in baseline[(text, "auto")]:
                    self.assertIn(dict(text=part["spokenText"], preparedText=part["preparedText"], key=part["key"]), background)
                self.assertEqual(baseline[(text, "cpp")], self.plan(text, voice="ef_dora"))
            self.assertEqual(snapshot, value)
        for invalid in ("true", 1, None):
            with self.subTest(invalid=invalid), self.assertRaisesRegex(AssertionError, "Invalid previous prose-inference policy"):
                self.plan(source, previousProseInference=invalid)
        with self.assertRaisesRegex(AssertionError, "restricted to bookmark planning"):
            self.worker.ask("curriculum_plan", lessonJson=json.dumps(lesson), page=0, language="es", voice="ef_dora",
                            previousProseInference=False)
        self.assertEqual(baseline[(source, "cpp")], self.plan(source, voice="ef_dora"))

    def test_prose_dictionary_names_require_source_syntax_or_explicit_semantics(self):
        import json
        samples = ("El tipo int conserva una unidad.", "La palabra return aparece en el texto.",
                   "El tipo array conserva un dato.", "El contenedor vector contiene datos.", "El carácter y_el continúa.")
        for source in samples:
            with self.subTest(source=source):
                current = self.plan(source, voice="ef_dora")
                self.assertEqual(source, " ".join(part["spokenText"] for part in current))
                prepared = " ".join(part["preparedText"] for part in current)
                self.assertNotIn("\ue000", prepared)
                self.assertNotIn("guion bajo", prepared)
                before = self.plan(source, voice="ef_dora", previousProseInference=True)
                self.assertNotEqual([part["key"] for part in current], [part["key"] for part in before])
                for policy in ("b42", "original"):
                    self.assertEqual(before, self.plan(source, voice="ef_dora", outputDataPolicy=policy))
                self.assertEqual(current, self.plan(source, voice="ef_dora"))
        source = samples[0]
        previous = self.plan(source, voice="ef_dora", previousProseInference=True)
        # The pre-fix bare nominal selected encode(code=false), whose exact
        # historical marker includes ~p; a code span uses encode(code=true).
        self.assertEqual(["\ue002El tipo \ue000int~p\ue001 conserva una unidad."], [part["preparedText"] for part in previous])
        # The overlay owns the whole field, including its gaps. Natural parts
        # never enter the lexical dictionary; explicit code still does, so this
        # frontend change must not alter any of their existing recordings.
        marked_source = "El tipo int conserva y; el dato array continúa."
        start = units("El tipo ")
        for role, reading in (("cpp", "auto"), ("string", "auto"), ("data", "natural"), ("prose", "natural")):
            semantics = descriptor(marked_source, role=role, reading=reading, start=start, end=start + 3)
            with self.subTest(role=role, reading=reading):
                current = self.plan(marked_source, semantics, voice="ef_dora")
                self.assertEqual(current, self.plan(marked_source, semantics, voice="ef_dora", previousProseInference=True))
                if role in ("cpp", "string"):
                    self.assertIn("\ue000int\ue001", " ".join(part["preparedText"] for part in current))
                else:
                    self.assertNotIn("\ue000", " ".join(part["preparedText"] for part in current))
        for code in ("`int y = 1;`", "int y = 1;", "std::cout << y;", "#include <iostream>",
                     "std::vector<int> valores;", "vector<int> valores;", "return 0;", "main()"):
            with self.subTest(code=code):
                current = self.plan(code, voice="ef_dora")
                self.assertEqual(current, self.plan(code, voice="ef_dora", previousProseInference=True))
                self.assertIn("\ue000", " ".join(part["preparedText"] for part in current))
        for code, semantics in (("foo_bar()", None), ("int foo_bar = 1;", None), ("`y_el`", None),
                                ("y_el", descriptor("y_el"))):
            with self.subTest(underscore_code=code, explicit=semantics is not None):
                current = self.plan(code, semantics, voice="ef_dora")
                self.assertEqual(current, self.plan(code, semantics, voice="ef_dora", previousProseInference=True))
                self.assertIn("guion bajo", " ".join(part["spokenText"] for part in current))
        blocks = [dict(type="paragraph", text=value) for value in samples]
        lesson = dict(id="qa-prose-lexical-inference", title="Datos y texto", pages=[dict(blocks=blocks)])
        background = self.worker.ask("curriculum_plan", lessonJson=json.dumps(lesson), page=0, language="es", voice="ef_dora",
                                    uiLanguage="es", contentLanguage="es", readingContext="cpp")["segments"]
        for source in samples:
            for part in self.plan(source, voice="ef_dora"):
                self.assertIn(dict(text=part["spokenText"], preparedText=part["preparedText"], key=part["key"]), background)

    def test_lesson_title_root_semantics_match_native_plans_without_block_layout(self):
        import json
        for source in ("😀\r\nCaracteres y y valores 7.", "😀\r\nCaracteres `y` y valores 7."):
            first = units(source[:source.index("y")])
            numeric = units(source[:source.index("7")])
            explicit = {"spans": descriptor(source, start=first, end=first + 1)["spans"] +
                        descriptor(source, role="data", reading="natural", start=numeric, end=numeric + 1)["spans"]}
            # Root authorship has no invented block owner or v2 requirement.
            declaration = dict(path="/title", sourceSha256=hashlib.sha256(source.encode()).hexdigest(), spans=explicit["spans"])
            lesson = dict(id="qa-root-title", title=source, pages=[dict(blocks=[])],
                          textSemantics=dict(version=1, fields=[], rootFields=[declaration]))
            snapshot = deepcopy(lesson)
            direct = self.plan(source, explicit, voice="ef_dora", readingContext="auto")
            self.assertEqual(1, " ".join(part["spokenText"] for part in direct).count("i griega"))
            expected = [dict(text=part["spokenText"], preparedText=part["preparedText"], key=part["key"]) for part in direct]
            for ui in ("es", "en", "fr"):
                with self.subTest(title=source, ui=ui):
                    reply = self.worker.ask("curriculum_plan", lessonJson=json.dumps(lesson), page=0, language="es",
                        voice="ef_dora", uiLanguage=ui, contentLanguage="es", readingContext="cpp")
                    self.assertEqual(expected, reply["segments"])
                    raw = source.encode("utf-16-le")
                    for part in direct:
                        self.assertEqual(part["text"], raw[2 * part["sourceStart"]:2 * part["sourceEnd"]].decode("utf-16-le"))
                        for shown in part["displayedRanges"]:
                            part["text"].encode("utf-16-le")[2 * shown["start"]:2 * shown["end"]].decode("utf-16-le")
                    self.assertEqual(snapshot, lesson)
            # No root annotation, a missing optional array and its empty form
            # retain the same existing title recording identity.
            plain = dict(id=lesson["id"], title=source, pages=lesson["pages"])
            plain_reply = self.worker.ask("curriculum_plan", lessonJson=json.dumps(plain), page=0, language="es",
                voice="ef_dora", uiLanguage="es", readingContext="cpp")["segments"]
            for overlay in (dict(version=1, fields=[]), dict(version=1, fields=[], rootFields=[])):
                value = dict(plain, textSemantics=overlay)
                self.assertEqual(plain_reply, self.worker.ask("curriculum_plan", lessonJson=json.dumps(value), page=0,
                    language="es", voice="ef_dora", uiLanguage="es", readingContext="cpp")["segments"])

    def test_lesson_title_root_owner_is_distinct_and_invalid_root_fields_are_rejected(self):
        import json
        source = "y"
        root = dict(path="/title", sourceSha256=hashlib.sha256(source.encode()).hexdigest(), spans=descriptor(source)["spans"])
        lesson = dict(id="qa-title-owner", title=source, pages=[dict(blocks=[dict(type="paragraph", text=source)]),
            dict(blocks=[dict(type="paragraph", text="Vecino.")])],
            stateLayout=dict(version=2, current=[dict(id="p0", blocks=["b0"]), dict(id="p1", blocks=["b1"])]),
            textSemantics=dict(version=1, fields=[field("b0", "/text", source, {"spans": []})], rootFields=[root]))
        def background(value, page=0):
            return self.worker.ask("curriculum_plan", lessonJson=json.dumps(value), page=page, language="es",
                voice="ef_dora", uiLanguage="es", readingContext="cpp")["segments"]
        def exported(plan):
            return [dict(text=part["spokenText"], preparedText=part["preparedText"], key=part["key"]) for part in plan]
        technical = exported(self.plan(source, descriptor(source), voice="ef_dora", readingContext="auto"))
        natural = exported(self.plan(source, {"spans": []}, voice="ef_dora", readingContext="auto"))
        before = background(lesson)
        self.assertEqual(technical + natural, before)
        swapped = deepcopy(lesson)
        swapped["textSemantics"]["rootFields"][0]["spans"] = []
        swapped["textSemantics"]["fields"][0]["spans"] = descriptor(source)["spans"]
        self.assertEqual(natural + technical, background(swapped))
        self.assertEqual(exported(self.plan("Vecino.", voice="ef_dora", readingContext="auto")), background(lesson, 1))
        invalid = []
        for patch in (dict(path="/id"), dict(sourceSha256="0" * 64), dict(block="b0"), dict(spans=[dict(start=0, end=1, text="z", role="cpp")])):
            wrong = deepcopy(lesson); wrong["textSemantics"]["rootFields"][0].update(patch); invalid.append(wrong)
        wrong = deepcopy(lesson); wrong["textSemantics"]["rootFields"].append(deepcopy(root)); invalid.append(wrong)
        wrong = deepcopy(lesson); wrong["textSemantics"]["rootFields"] = None; invalid.append(wrong)
        wrong = deepcopy(lesson); del wrong["textSemantics"]["fields"]; invalid.append(wrong)
        for value in invalid:
            with self.subTest(rootFields=value["textSemantics"]), self.assertRaisesRegex(AssertionError, "Narration failed"):
                background(value)
        self.assertEqual(before, background(lesson))

    def test_image_alt_and_caption_are_independent_authored_regions_even_when_equal(self):
        import json
        for same in (False, True):
            alt, caption = "y", "y" if same else "Otro y."
            value = dict(id="qa-image-text", pages=[dict(blocks=[dict(type="image", imageId="qa_image",
                src="images/qa_image.png", alt=alt, caption=caption)])],
                stateLayout=dict(version=2, current=[dict(id="page", blocks=["image"])]),
                textSemantics=dict(version=1, fields=[field("image", "/alt", alt, descriptor(alt)),
                    field("image", "/caption", caption, {"spans": []})]))
            snapshot = deepcopy(value)
            def expected(source, semantics):
                return [dict(text=part["spokenText"], preparedText=part["preparedText"], key=part["key"])
                    for part in self.plan(source, semantics, voice="ef_dora", readingContext="auto")]
            foreground = expected(alt, descriptor(alt)) + expected(caption, {"spans": []})
            with self.subTest(same_source=same):
                background = self.worker.ask("curriculum_plan", lessonJson=json.dumps(value), page=0, language="es",
                    voice="ef_dora", uiLanguage="es", contentLanguage="es", readingContext="cpp")["segments"]
                self.assertEqual(foreground, background)
                self.assertEqual(snapshot, value)
                self.assertNotIn("qa_image", " ".join(part["text"] for part in background))
                # An optional missing caption emits no synthetic label or copy
                # of alt, and preserves the exact existing alternative text.
                without = deepcopy(value)
                del without["pages"][0]["blocks"][0]["caption"]
                without["textSemantics"]["fields"] = without["textSemantics"]["fields"][:1]
                self.assertEqual(expected(alt, descriptor(alt)), self.worker.ask("curriculum_plan", lessonJson=json.dumps(without),
                    page=0, language="es", voice="ef_dora", uiLanguage="es", readingContext="cpp")["segments"])

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
