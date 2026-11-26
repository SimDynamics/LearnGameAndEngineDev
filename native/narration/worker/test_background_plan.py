"""Background preparation uses the original script and unchanged cache keys."""
import hashlib
import copy
import json
import math
from pathlib import Path
import re
import struct
import subprocess
import unittest
import wave
import xml.etree.ElementTree as ET
from test_worker import Worker, CACHE, ROOT


def native_math_help(response_type):
    """Read the exact three translations registered as Win32 reading regions.

    This is independent of the generated JVM adapter. A separate native test
    mounts MathPuzzleView and contrasts its actual reading_regions at runtime.
    """
    source = (ROOT / "native/presentation/windows/math_puzzle_view.cpp").read_text(encoding="utf-8-sig")
    start = source.index(f'else if(type=="{response_type}")')
    end = source.index("canvas(config,", start)
    literal = r'"(?:[^"\\]|\\.)*"'
    pattern = rf'add_text\(tr\(\s*({literal})\s*,\s*({literal})\s*,\s*({literal})\s*\),\s*id\s*\+\s*"/(help|legend)"\s*\)'
    rows = re.findall(pattern, source[start:end])
    if len(rows) != 2 or [row[3] for row in rows] != ["help", "legend"]:
        raise AssertionError("The two native math reading regions changed; inspect their real source")
    return {role: dict(zip(("es", "en", "fr"), map(json.loads, strings)))
            for *strings, role in rows}


class WindowsMathHelpBackgroundPlanTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        # Planning needs no models and never synthesizes or rewrites a WAV.
        cls.worker = Worker(missing_resources=True)

    @classmethod
    def tearDownClass(cls):
        cls.worker.close()

    @staticmethod
    def response(response_type):
        if response_type == "math_segment_transport":
            validator = {"type": "segment_transport", "mode": "chain", "sources": [
                {"id": "ab", "from": {"x": 0, "y": 3, "label": "A"},
                 "to": {"x": 4, "y": 3, "label": "B"}}],
                "target": {"origin": {"x": 0, "y": 0, "label": "M"},
                           "direction": {"x": 1, "y": 0}},
                "steps": ["ab"], "pointLabels": ["N"],
                "positionTolerance": .04, "lengthTolerance": .04}
        else:
            def figure(vertex, first, second):
                return {"vertex": vertex, "sides": ["first", "second"], "region": "counterclockwise",
                    "diagram": {"points": [{"id": vertex, "x": 0, "y": 0, "labels": [vertex]},
                        {"id": first, "x": 4, "y": 0, "labels": [first]},
                        {"id": second, "x": 0, "y": 3, "labels": [second]}],
                        "objects": [{"id": name, "kind": "ray", "through": [vertex, end],
                                     "contains": [vertex, end]}
                                    for name, end in (("first", first), ("second", second))]}}
            validator = {"type": "rigid_angle_copy", "reference": figure("O", "A", "B"),
                "copy": figure("V", "C", "D"), "initial": {"x": 7, "y": 4, "rotation": .6},
                "relation": "congruent", "positionTolerance": .1, "directionTolerance": .02}
        validator["expected"] = "secretvalidatorqqq"
        return {"id": "construction", "type": response_type, "validator": validator,
                "label": "Visible construction", "tags": ["secrettagqqq"]}

    def plan(self, source, language, voice):
        return self.worker.ask("plan", text=source, language=language, voice=voice)["segments"]

    def test_native_help_and_legend_keep_all_visible_languages_tags_and_cache_keys(self):
        contexts = [("es", "es", "em_santa"), ("en", "en-US", "af_heart"),
                    ("fr", "fr", "ff_siwis"), ("en-GB", "en-GB", "bf_emma"),
                    ("es", "en-US", "af_heart"), ("fr", "en-GB", "bf_emma")]
        for response_type in ("math_segment_transport", "math_rigid_angle_copy"):
            source = native_math_help(response_type)
            for ui, language, voice in contexts:
                visible_language = "fr" if ui == "fr" else "en" if ui.startswith("en") else "es"
                expected = [item for role in ("help", "legend")
                            for item in self.plan(source[role][visible_language], language, voice)]
                expected_keys = [item["key"] for item in expected]
                self.assertTrue(expected_keys)
                for standalone in (False, True):
                    with self.subTest(response=response_type, ui=ui, language=language, standalone=standalone):
                        response = self.response(response_type)
                        block = response if standalone else {"type": "math_puzzle", "responses": [response]}
                        block.update(prompt="Visible neutral prompt", feedback={"correct": "Visible success",
                            "incorrect": "secretretryqqq"})
                        background = self.worker.ask("curriculum_plan", lessonJson=json.dumps({"pages": [
                            {"blocks": [block]}]}, ensure_ascii=False), page=0, language=language,
                            voice=voice, uiLanguage=ui)["segments"]
                        actual_keys = [item["key"] for item in background]
                        self.assertTrue(any(actual_keys[index:index + len(expected_keys)] == expected_keys
                            for index in range(len(actual_keys))),
                            "Background omitted or changed the complete ordered help/legend of the native reading regions")
                        serialized = json.dumps(background, ensure_ascii=False)
                        for secret in ("secretvalidatorqqq", "secretretryqqq", "secrettagqqq"):
                            self.assertNotIn(secret, serialized)
                            private_keys = {item["key"] for item in self.plan(secret, language, voice)}
                            self.assertTrue(private_keys.isdisjoint(actual_keys),
                                "Private metadata entered the background plan after speech normalization")
                        self.assertIn("Visible success", " ".join(item["text"] for item in background))

    def test_rigid_help_prepares_windows_double_click_not_android_two_fingers(self):
        source = native_math_help("math_rigid_angle_copy")["help"]
        for ui, language, voice, folder in [("es", "es", "em_santa", "values"),
                ("en", "en-US", "af_heart", "values-en"), ("fr", "fr", "ff_siwis", "values-fr")]:
            with self.subTest(ui=ui):
                mobile = ET.parse(ROOT / "app/src/main/res" / folder / "rigid_angle_strings.xml") \
                    .find("string[@name='math_rigid_help']").text
                visible_keys = {item["key"] for item in self.plan(source[ui], language, voice)}
                old_only = {item["key"] for item in self.plan(mobile, language, voice)} - visible_keys
                self.assertTrue(old_only, "The native/mobile gesture contrast became vacuous")
                block = {"type": "math_puzzle", "responses": [self.response("math_rigid_angle_copy")]}
                background = self.worker.ask("curriculum_plan", lessonJson=json.dumps({"pages": [
                    {"blocks": [block]}]}), page=0, language=language, voice=voice, uiLanguage=ui)["segments"]
                self.assertTrue(old_only.isdisjoint(item["key"] for item in background),
                    "Background still prepared the Android-only two-finger instruction")


class WindowsMathHelpGenerationTest(unittest.TestCase):
    def test_native_reading_region_extraction_fails_closed_without_writing_sources(self):
        from build_worker import windows_math_ui_texts
        path = ROOT / "native/presentation/windows/math_puzzle_view.cpp"
        source = path.read_text(encoding="utf-8-sig")
        actual = windows_math_ui_texts(source)
        for response_type in ("math_segment_transport", "math_rigid_angle_copy"):
            expected = native_math_help(response_type)
            for role in ("help", "legend"):
                self.assertEqual(tuple(expected[role][language] for language in ("es", "en", "fr")),
                                 actual[f"{response_type}:{role}"])
        for malformed in (source.replace('else if(type=="math_segment_transport")', "else if(false)", 1),
                source.replace('id+"/help"', 'id+"/private_helper"', 1),
                source + '\nelse if(type=="math_rigid_angle_copy")'):
            with self.assertRaises(ValueError):
                windows_math_ui_texts(malformed)

    def test_generated_kotlin_keeps_native_dollar_and_unicode_literal(self):
        from build_worker import kotlin_literal
        self.assertEqual(r'"\$dato \u00e9\n\u000c"', kotlin_literal("$dato é\n\f"))
        self.assertEqual(r'"\\foo \\\$literal"', kotlin_literal(r"\foo \$literal"))

    def test_native_translation_changes_invalidate_runtime_without_geometry_dependency(self):
        from build_worker import windows_math_ui_texts, windows_math_ui_fingerprint
        source = (ROOT / "native/presentation/windows/math_puzzle_view.cpp").read_text(encoding="utf-8-sig")
        before = windows_math_ui_fingerprint(source)
        self.assertEqual(before, windows_math_ui_fingerprint(source + "\n// Unrelated native drawing change.\n"))
        for key, translations in windows_math_ui_texts(source).items():
            for index, text in enumerate(translations):
                with self.subTest(region=key, language=index):
                    self.assertIn(text, source)
                    self.assertNotEqual(before, windows_math_ui_fingerprint(source.replace(text, text + " revised", 1)))


class BackgroundPlanTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.worker = Worker()

    @classmethod
    def tearDownClass(cls):
        cls.worker.close()

    def test_script_excludes_validator_code_and_incorrect_feedback(self):
        lesson = {"title": "Test", "pages": [{"blocks": [
            {"type": "paragraph", "text": "Une phrase continue."},
            {"type": "code", "code": "NEVER_SPEAK_CODE"},
            {"type": "multiple_choice", "question": "Choisissez une réponse.",
             "options": ["Première", "Deuxième"], "correctAnswer": "NEVER_SPEAK_ANSWER",
             "explanation": "Une explication correcte.", "reviewFeedback": "NEVER_SPEAK_INCORRECT"}]}]}
        plan = self.worker.ask("curriculum_plan", lessonJson=json.dumps(lesson), page=0,
                               language="fr", voice="ff_siwis", uiLanguage="fr")
        spoken = " ".join(s["text"] for s in plan["segments"])
        self.assertNotIn("NEVER_SPEAK", spoken)
        self.assertLess(spoken.index("Première"), spoken.index("Deuxième"))
        self.assertIn("explication correcte", spoken)

    def test_real_audio_cache_shared_with_visible_source_plan(self):
        for language, voice, source in [
            ("es", "em_santa", "Esta página conserva la voz seleccionada."),
            ("fr", "ff_siwis", "Cette page conserve la voix choisie."),
            ("en-US", "af_heart", "This page keeps the selected voice."),
            ("en-GB", "bf_emma", "This page keeps the selected voice.")]:
            with self.subTest(language=language):
                lesson = {"pages": [{"blocks": [{"type": "paragraph", "text": source}]}]}
                background = self.worker.ask("curriculum_plan", lessonJson=json.dumps(lesson), page=0,
                                             language=language, voice=voice, uiLanguage=language)["segments"]
                self.assertEqual(1, len(background))
                before = self.worker.ask("prepare", text=background[0]["text"],
                    preparedText=background[0]["preparedText"], language=language, voice=voice,
                    outputDirectory=str(CACHE), threads=2, audioOnly=True)
                wav = Path(before["wav"])
                digest = hashlib.sha256(wav.read_bytes()).digest()
                stamp = wav.stat().st_mtime_ns
                visible = self.worker.ask("plan", text=source, language=language, voice=voice)["segments"][0]
                self.assertEqual(background[0]["key"], visible["key"])
                after = self.worker.ask("prepare", text=visible["text"],preparedText=visible["preparedText"],
                    language=language,voice=voice,outputDirectory=str(CACHE),threads=2)
                self.assertTrue(after["cacheHit"])
                self.assertEqual(digest,hashlib.sha256(wav.read_bytes()).digest())
                self.assertEqual(stamp,wav.stat().st_mtime_ns)
                self.assertGreater(len(after["timings"]),2)

    def test_real_native_math_help_audio_is_shared_with_visible_reader(self):
        cases = [("math_segment_transport", "es", "es", "em_santa"),
                 ("math_rigid_angle_copy", "fr", "fr", "ff_siwis"),
                 ("math_segment_transport", "en", "en-US", "af_heart"),
                 ("math_rigid_angle_copy", "en-GB", "en-GB", "bf_emma")]
        for response_type, ui, language, voice in cases:
            with self.subTest(response=response_type, language=language, voice=voice):
                translations = native_math_help(response_type)["help"]
                source = translations["en" if ui.startswith("en") else ui]
                visible = self.worker.ask("plan", text=source, language=language, voice=voice)["segments"][0]
                lesson = {"pages": [{"blocks": [{"type": "math_puzzle", "responses": [
                    WindowsMathHelpBackgroundPlanTest.response(response_type)]}]}]}
                background = self.worker.ask("curriculum_plan", lessonJson=json.dumps(lesson), page=0,
                    language=language, voice=voice, uiLanguage=ui)["segments"]
                # Visible source spans retain inter-sentence whitespace for ink;
                # background stores the spoken text. Compare that exact text,
                # but later align against the original, untrimmed reader span.
                identity = {"text": visible["spokenText"], "preparedText": visible["preparedText"],
                            "key": visible["key"]}
                matches = [item for item in background if all(item[field] == value for field, value in identity.items())]
                self.assertEqual(1, len(matches), "The real visible help fragment must occur once in anticipation")
                anticipated = matches[0]
                before = self.worker.ask("prepare", text=anticipated["text"],
                    preparedText=anticipated["preparedText"], language=language, voice=voice,
                    outputDirectory=str(CACHE), threads=2, audioOnly=True)
                wav = Path(before["wav"])
                audio_bytes = wav.read_bytes()
                audio_hash = hashlib.sha256(audio_bytes).hexdigest()
                stamp = wav.stat().st_mtime_ns
                self.assertEqual(visible["key"], before["key"])
                with wave.open(str(wav), "rb") as audio:
                    self.assertEqual((1, 24000, 2),
                        (audio.getnchannels(), audio.getframerate(), audio.getsampwidth()))
                    self.assertEqual(before["sampleCount"], audio.getnframes())
                    self.assertGreater(audio.getnframes(), 2400)
                    samples = struct.unpack("<" + "h" * audio.getnframes(), audio.readframes(audio.getnframes()))
                    self.assertGreater(max(map(abs, samples)), 50, "The cached help must contain real non-silent PCM")
                after = self.worker.ask("prepare", text=visible["text"], preparedText=visible["preparedText"],
                    language=language, voice=voice, outputDirectory=str(CACHE), threads=2)
                self.assertTrue(after["cacheHit"])
                self.assertEqual(before["key"], after["key"])
                self.assertEqual(wav, Path(after["wav"]))
                self.assertEqual(audio_bytes, wav.read_bytes())
                self.assertEqual(stamp, wav.stat().st_mtime_ns)
                self.assertTrue(after["alignmentAvailable"])
                self.assertGreater(len(after["timings"]), 2)
                previous = -1.0
                for row in after["timings"]:
                    self.assertTrue(0 <= row["start"] < row["end"] <= len(visible["text"]))
                    self.assertTrue(math.isfinite(row["startMs"]) and math.isfinite(row["endMs"]))
                    self.assertTrue(previous <= row["startMs"] < row["endMs"] <= after["durationMs"])
                    previous = row["startMs"]
                print("MATH_HELP_CACHE " + json.dumps({"type": response_type, "language": language,
                    "voice": voice, "text": visible["text"], "key": after["key"], "sha256": audio_hash,
                    "firstCacheHit": before["cacheHit"], "readerCacheHit": after["cacheHit"],
                    "sampleCount": after["sampleCount"], "timings": len(after["timings"]),
                    "wav": str(wav)}, ensure_ascii=False), flush=True)

    def test_technical_and_math_scripts_keep_visible_reader_cache_identity(self):
        for language,voice,source in [
            ("es","em_santa","La línea #include <iostream> permite usar std::cout; el resultado es 6 × 100 = 600."),
            ("fr","ff_siwis","La ligne #include <iostream> permet d'utiliser std::cout ; le résultat est 6 × 100 = 600."),
            ("en-US","af_heart","The line #include <iostream> allows std::cout; the result is 6 × 100 = 600."),
            ("en-GB","bf_emma","The line #include <iostream> allows std::cout; the result is 6 × 100 = 600.")]:
            with self.subTest(language=language):
                lesson={"pages":[{"blocks":[{"type":"paragraph","text":source}]}]}
                background=self.worker.ask("curriculum_plan",lessonJson=json.dumps(lesson),page=0,
                    language=language,voice=voice,uiLanguage=language)["segments"]
                visible=self.worker.ask("plan",text=source,language=language,voice=voice)["segments"]
                self.assertEqual([s["key"] for s in visible],[s["key"] for s in background])


class WindowsPageNarrationRevisionTest(unittest.TestCase):
    """No models: source/page isolation and hidden-success script ownership."""
    @classmethod
    def setUpClass(cls):
        cls.worker = Worker(missing_resources=True)

    @classmethod
    def tearDownClass(cls):
        cls.worker.close()

    @staticmethod
    def lesson():
        return {"id": "revision-fixture", "title": "Una lección.", "pages": [
            {"blocks": [{"type": "paragraph", "text": "La página cambia."},
                        {"type": "multiple_choice", "question": "Elige el nombre.",
                         "options": ["saldo", "total"], "correctIndex": 0,
                         "explanation": "El nombre correcto es saldo."}]},
            {"blocks": [{"type": "paragraph", "text": "Esta página se conserva."}]}],
            "stateLayout": {"version": 2, "current": [
                {"id": "first", "blocks": ["intro", "exercise"]},
                {"id": "second", "blocks": ["unchanged"]}]}}

    def plan(self, lesson, page=0, **fields):
        return self.worker.ask("curriculum_plan", lessonJson=json.dumps(lesson, ensure_ascii=False),
            page=page, language="es", voice="ef_dora", uiLanguage="es", **fields)

    def test_complete_published_assetcheck_history_exceeds_old_transport_limit(self):
        relative = ("app/src/main/assets/content/es/programming/programming_practice_tools/"
                    "proyecto_assetcheck/lesson_disenar_probar_y_presentar_variante_propia.json")
        commit = "70e71e5e1876f3245633d92c414dda7f358a75ba"
        payload = subprocess.check_output(["git", "show", f"{commit}:{relative}"], cwd=ROOT)
        self.assertEqual(hashlib.sha1(b"blob " + str(len(payload)).encode() + b"\0" + payload).hexdigest(),
                         "2b58163adeade4bb27da4ca45f9407397aa72e02")
        raw = payload.decode("utf-8")
        lesson = json.loads(raw)
        self.assertEqual(len(lesson["pages"]), 18)
        envelope = json.dumps({"id": "historical-qa", "command": "curriculum_plan",
                               "lessonJson": raw}, ensure_ascii=False)
        self.assertGreater(len(envelope.encode("utf-16-le")) // 2, 2_000_000)
        # Use the authentic pretty-printed publication in every request.
        # The compact serialization is merely a comparison, not a repair.
        compact = json.dumps(lesson, ensure_ascii=False, separators=(",", ":"))
        for page in range(len(lesson["pages"])):
            with self.subTest(page=page):
                first = self.worker.ask("curriculum_plan", lessonJson=raw, page=page,
                                        language="es", voice="ef_dora", uiLanguage="es")
                self.assertRegex(first["revision"], r"^[0-9a-f]{64}$")
                if page in (0, 8, 17):
                    same = self.worker.ask("curriculum_plan", lessonJson=compact, page=page,
                                           language="es", voice="ef_dora", uiLanguage="es")
                    self.assertEqual(first["revision"], same["revision"])
                    self.assertEqual(first["segments"], same["segments"])
        # The old ordinary-command limit remains, but its error must carry
        # the exact caller ID and must not poison a following small request.
        identity = self.worker.send("plan", text="x" * 2_000_001, language="es")
        error = self.worker.receive()
        self.assertEqual((identity, "plan"), (error["id"], error["command"]))
        self.assertIn("too large", error["error"])
        self.assertTrue(self.worker.ask("plan", text="Lectura recuperada.", language="es")["segments"])

    def test_every_page_source_change_revises_entire_script_without_touching_other_page(self):
        original = self.lesson()
        before, other = self.plan(original), self.plan(original, 1)
        self.assertRegex(before["revision"], r"^[0-9a-f]{64}$")
        self.assertEqual("revision-fixture/es/first", before["pageId"])
        variants = []
        for change in ("text", "question", "explanation", "options", "order", "new_puzzle", "removed_puzzle"):
            lesson = copy.deepcopy(original)
            blocks = lesson["pages"][0]["blocks"]
            if change == "text": blocks[0]["text"] = "La explicación cambia."
            elif change == "question": blocks[1]["question"] = "Elige ahora el nombre."
            elif change == "explanation": blocks[1]["explanation"] = "Ahora continúa el siguiente título."
            elif change == "options": blocks[1]["options"][1] = "importe"
            elif change == "order": blocks.reverse(); lesson["stateLayout"]["current"][0]["blocks"].reverse()
            elif change == "new_puzzle": blocks.append(copy.deepcopy(blocks[1])); lesson["stateLayout"]["current"][0]["blocks"].append("new")
            else: blocks.pop(); lesson["stateLayout"]["current"][0]["blocks"].pop()
            variants.append((change, lesson))
        for change, lesson in variants:
            with self.subTest(change=change):
                self.assertNotEqual(before["revision"], self.plan(lesson)["revision"])
                self.assertEqual(other["revision"], self.plan(lesson, 1)["revision"])
                self.assertEqual(before["pageId"], self.plan(lesson)["pageId"])

    def test_semantics_exact_numbers_and_prepared_script_settings_are_versioned(self):
        original = self.lesson()
        before = self.plan(original)
        semantic = copy.deepcopy(original)
        source = semantic["pages"][0]["blocks"][0]["text"]
        semantic["textSemantics"] = {"version": 1, "fields": [{"block": "intro", "path": "/text",
            "sourceSha256": hashlib.sha256(source.encode()).hexdigest(), "spans": [
                {"start": 0, "end": 2, "text": "La", "role": "prose", "reading": "natural"}]}]}
        self.assertNotEqual(before["revision"], self.plan(semantic)["revision"])
        self.assertEqual(self.plan(original, 1)["revision"], self.plan(semantic, 1)["revision"])
        self.assertNotEqual(before["revision"], self.plan(original, readingContext="cpp")["revision"])
        first, second = copy.deepcopy(original), copy.deepcopy(original)
        first["pages"][0]["blocks"][0]["exactQA"] = 9007199254740992
        second["pages"][0]["blocks"][0]["exactQA"] = 9007199254740993
        self.assertNotEqual(self.plan(first)["revision"], self.plan(second)["revision"])

    def test_formatting_key_order_and_other_page_title_scope_preserve_revision(self):
        lesson = self.lesson()
        before = self.plan(lesson)
        raw = json.dumps(lesson, ensure_ascii=False, sort_keys=True, indent=3)
        reordered = self.worker.ask("curriculum_plan", lessonJson=raw, page=0,
            language="es", voice="ef_dora", uiLanguage="es")
        self.assertEqual(before["revision"], reordered["revision"])
        second = self.plan(lesson, 1)
        lesson["title"] = "Otro título de la misma lección."
        self.assertNotEqual(before["revision"], self.plan(lesson)["revision"])
        self.assertEqual(second["revision"], self.plan(lesson, 1)["revision"])


if __name__ == "__main__":
    unittest.main()
