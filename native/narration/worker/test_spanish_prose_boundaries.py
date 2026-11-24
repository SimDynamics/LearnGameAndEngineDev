"""Real Windows plans, provenance and Kokoro audio at C++/Spanish boundaries.

Fixtures never edit curriculum, user state or recordings. Generated samples are
kept for human listening; PCM/timing checks do not assert pronunciation quality.
"""
from __future__ import annotations

import copy
import hashlib
import json
from pathlib import Path
import re
import struct
import sys
import time
import unittest
import wave

from test_worker import ROOT, Worker

sys.path.insert(0, str(ROOT / "scripts/curriculum"))
import course_edition as edition
import stable_state_layout as layout
from test_pending_presentation_contract import resealed_edition_fixture, selected_u2_edition_lesson


ASSET = ROOT / "app/src/main/assets/content/es/programming/cpp_fundamentals/objetos_y_estado/lesson_estado_asignacion_y_copia.json"
U11_ASSET = ROOT / "app/src/main/assets/content/es/programming/cpp_fundamentals/texto_interpretacion_y_flujos/lesson_diagnosticar_y_recuperar_hasta_fin_de_operacion.json"
OUT = ROOT / "out/qa/spanish-prose-boundaries"
VOICES = ("ef_dora", "em_alex", "em_santa")
# Exact historical explanation from 22443d8, independently checked before the fix.
ORIGINAL = "++turnos aumenta en una unidad el valor existente. turnos = 1 lo reemplaza por uno; int turnos = 1 es una definición, no una actualización del objeto existente."
ORIGINAL_SHA = "cb58def4757e3816009b0faa7f55930094a73d28f95405ae98a2f2de8976ef77"
CURRENT_PREPARED = (
    "\ue002Correcto incremento turnos punto y coma aumenta en una unidad el valor existente.",
    "\ue002turnos asigna uno punto y coma lo reemplaza por uno.",
    "\ue002\ue000int\ue001 turnos asigna uno punto y coma es una definición, no una actualización del objeto existente.",
)
# The selected U2L9 publication separates the old mixed paragraph into authored
# prose and read-only code. Pin the new introductory prose independently of a
# candidate's freshly resealed stateLayout (publication blob ec4f24abaa63a14699f7e51e65003b472c7aacf4).
PROMPT_INTRODUCTION_SHA = "2ac6601112f36bc5f4246107c9b88e86ac064d05f8ae5f6244329ff6b7aff4de"
PROMPT_INSTRUCTION_OWNER = "p1u2l9_ejemplo_0001_0001_1"
PROMPT_OUTPUT_OWNER = "p1u2l9_ejemplo_0001_0001_2"


def units(text):
    return len(text.encode("utf-16-le")) // 2


def slice_units(text, first, last):
    return text.encode("utf-16-le")[first * 2:last * 2].decode("utf-16-le")


def owns_word(text, owner, first, last):
    # The production reading owner can include a following comma/space before
    # the next word. It must contain the WHOLE word without another identifier.
    if owner["start"] > first or owner["end"] < last:
        return False
    extra = slice_units(text, owner["start"], first) + slice_units(text, last, owner["end"])
    return all(not character.isalnum() and character != "_" for character in extra)


def authored_prompt_field(pages, current, semantics, owner="block_0001_0001"):
    matches = [block for page, row in zip(pages, current)
               for identity, block in zip(row["blocks"], page["blocks"])
               if identity == owner]
    if len(matches) != 1 or matches[0].get("type") != "paragraph":
        raise AssertionError("The U2L9 prompt owner is missing or ambiguous")
    source = matches[0]["text"]
    fields = [field for field in semantics["fields"]
              if field["block"] == owner and field["path"] == "/text"]
    if len(fields) != 1 or fields[0]["sourceSha256"] != hashlib.sha256(source.encode("utf-8")).hexdigest():
        raise AssertionError("The prompt semantic field is not bound to its exact source")
    for span in fields[0]["spans"]:
        if not 0 <= span["start"] < span["end"] <= units(source) or \
                slice_units(source, span["start"], span["end"]) != span["text"]:
            raise AssertionError("The prompt semantic span lost its UTF-16 source owner")
    return source, fields[0]


def prompt_fragment(source, field, first, last):
    spans = []
    for span in field["spans"]:
        if span["end"] <= first or span["start"] >= last:
            continue
        if not first <= span["start"] < span["end"] <= last:
            raise AssertionError("An isolated prompt fixture split an authored semantic span")
        spans.append(span | {"start": span["start"] - first, "end": span["end"] - first})
    return {"text": slice_units(source, first, last), "textSemantics": {"spans": spans}}


def selected_prompt_documents():
    content = ROOT / "app/src/main/assets/content/es/programming/cpp_fundamentals"
    path, active, originals = selected_u2_edition_lesson(content, "leer_una_entrada_conocida", 9)
    historical_path = content / "objetos_y_estado/lesson_leer_una_entrada_conocida.json"
    originals[historical_path] = historical_path.read_bytes()
    historical = edition._parse(originals[historical_path].decode("utf-8-sig"))
    return path, active, historical_path, historical, originals


def prompt_fixtures(lesson, historical_lesson):
    value = edition.validate_lesson(lesson, require_layout=True)
    if value is None or value["courseId"] != "cpp_fundamentals" or value["locale"] != "es" or \
            lesson["id"] != edition.unit_prefix(value) + "objetos_y_estado__leer_una_entrada_conocida":
        raise AssertionError("The prompt must come from the selected Spanish U2L9 edition")
    if historical_lesson.get("id") != "cpp_fundamentals__objetos_y_estado__leer_una_entrada_conocida" or \
            edition.descriptor(historical_lesson) is not None:
        raise AssertionError("The archived prompt must remain its separate legacy publication")
    for document in (lesson, historical_lesson):
        layout.validate(document)
        layout.text_semantics.validate(document)
    source, field = authored_prompt_field(
        lesson["pages"], lesson["stateLayout"]["current"], lesson["textSemantics"])
    meta = historical_lesson["stateLayout"]
    retained_source, retained_field = authored_prompt_field(
        historical_lesson["pages"], meta["current"], historical_lesson["textSemantics"])
    instruction_source, instruction_field = retained_source, retained_field
    blocks = layout._blocks(lesson)
    if not {PROMPT_INSTRUCTION_OWNER, PROMPT_OUTPUT_OWNER} <= blocks.keys():
        raise AssertionError("The current prompt must retain both authored split owners")
    if field["sourceSha256"] != PROMPT_INTRODUCTION_SHA or field["spans"] != []:
        raise AssertionError("The current prompt introduction lost its authenticated explicit prose")
    instruction = blocks[PROMPT_INSTRUCTION_OWNER]
    if instruction.get("type") != "code" or instruction.get("language") != "cpp" or \
            instruction.get("code") != 'std::cout << "Cantidad: ";':
        raise AssertionError("The current read-only instruction differs from its authenticated source")
    source, field = authored_prompt_field(
        lesson["pages"], lesson["stateLayout"]["current"], lesson["textSemantics"], PROMPT_OUTPUT_OWNER)
    first = retained_source.index("\n") + 1
    last = retained_source.index("\n\n", first)
    expected = prompt_fragment(retained_source, retained_field,
                               units(retained_source[:first]), units(retained_source[:last]))
    if source != expected["text"] or field["spans"] != expected["textSemantics"]["spans"]:
        raise AssertionError("The current output paragraph differs from its authenticated fragment or UTF-16 meaning")
    result = {}
    for name, text, role, reading in (
            ("current-instruction", 'std::cout << "Cantidad: ";', "cpp", "technical"),
            ("current-output", "Cantidad:", "output", "natural")):
        # The instruction is an isolated worker sample, authenticated against
        # the actual code card and the retained technical span. Read-only code
        # cards are not automatically narrated by LessonReader. The output
        # remains the real authored natural-reading span of today's paragraph.
        sample_source, sample_field = ((instruction_source, instruction_field)
                                       if name == "current-instruction" else (source, field))
        selected = [span for span in sample_field["spans"]
                    if (span["text"], span["role"], span["reading"]) == (text, role, reading)]
        if len(selected) != 1:
            raise AssertionError(f"Missing exact current prompt meaning: {name}")
        span = selected[0]
        result[name] = prompt_fragment(sample_source, sample_field, span["start"], span["end"])
    historical = []
    for snapshot in meta.get("retirementHistory", []):
        old_source, old_field = authored_prompt_field(
            snapshot["pages"], snapshot["current"], snapshot["textSemantics"])
        if old_field["sourceSha256"] == "e51751958497efb747b725f1126650e4431409777fbd87c70401e1638cfbdf9e":
            historical.append((old_source, old_field))
    if not historical:
        raise AssertionError("The original archived prompt fixture was lost")
    old_source, old_field = historical[0]
    first = old_source.index("Por ejemplo, ")
    last = old_source.index(". ", first) + 1
    result["historical-prompt"] = prompt_fragment(
        old_source, old_field, units(old_source[:first]), units(old_source[:last]))
    if result["historical-prompt"]["text"] != "Por ejemplo, `Cantidad: ` puede avisar de que se recibirá una cantidad.":
        raise AssertionError("The original archived prompt sentence changed")
    # Preserve the original unmarked-prose regression in both auto/cpp contexts.
    result["conjunction"] = {"text": "El texto contiene letras y palabras.", "textSemantics": None}
    return result


class PromptEditionSourceTest(unittest.TestCase):
    """Portable source checks; no worker process or audio is substituted."""

    def test_selected_edition_and_archived_prompt_keep_independent_exact_owners(self):
        path, active, historical_path, historical, originals = selected_prompt_documents()
        before = copy.deepcopy((active, historical))
        self.assertNotEqual(path, historical_path)
        self.assertNotEqual(active["id"], historical["id"])
        fixtures = prompt_fixtures(active, historical)
        self.assertEqual(set(fixtures), {"current-instruction", "current-output", "historical-prompt", "conjunction"})
        self.assertEqual(fixtures["current-instruction"]["text"], 'std::cout << "Cantidad: ";')
        self.assertEqual(fixtures["current-output"]["text"], "Cantidad:")
        for name, fixture in fixtures.items():
            if name == "conjunction":
                self.assertIsNone(fixture["textSemantics"])
                continue
            for span in fixture["textSemantics"]["spans"]:
                self.assertEqual(slice_units(fixture["text"], span["start"], span["end"]), span["text"])
        self.assertEqual((active, historical), before)
        for source, original in originals.items():
            self.assertEqual(source.read_bytes(), original)

    def test_active_prompt_cannot_reseal_changed_text_or_reading_as_its_source(self):
        _, active, _, historical, _ = selected_prompt_documents()
        self.assertEqual(prompt_fixtures(resealed_edition_fixture(active), historical),
                         prompt_fixtures(active, historical))
        for identity, path, message in (
                ("block_0001_0001", "text", "authenticated explicit prose"),
                (PROMPT_OUTPUT_OWNER, "text", "authenticated fragment"),
                (PROMPT_INSTRUCTION_OWNER, "code", "read-only instruction")):
            with self.subTest(owner=identity):
                changed = copy.deepcopy(active)
                owner = layout._blocks(changed)[identity]
                owner[path] += " Texto sin procedencia."
                if path == "text":
                    marker = next(field for field in changed["textSemantics"]["fields"]
                                  if (field["block"], field["path"]) == (identity, "/text"))
                    marker["sourceSha256"] = hashlib.sha256(owner[path].encode("utf-8")).hexdigest()
                changed = resealed_edition_fixture(changed)
                layout.text_semantics.validate(changed)
                with self.assertRaisesRegex(AssertionError, message):
                    prompt_fixtures(changed, historical)
        changed = copy.deepcopy(active)
        marker = next(field for field in changed["textSemantics"]["fields"]
                      if (field["block"], field["path"]) == (PROMPT_OUTPUT_OWNER, "/text"))
        marker["spans"][0].update(role="prose", reading="natural")
        layout.text_semantics.validate(changed)
        with self.assertRaisesRegex(AssertionError, "authenticated fragment"):
            prompt_fixtures(changed, historical)
        changed = copy.deepcopy(active)
        source = layout._blocks(changed)["block_0001_0001"]["text"]
        marker = next(field for field in changed["textSemantics"]["fields"]
                      if (field["block"], field["path"]) == ("block_0001_0001", "/text"))
        first = source.index("programa")
        marker["spans"].append({"start": units(source[:first]), "end": units(source[:first + len("programa")]),
                                "text": "programa", "role": "cpp", "reading": "technical"})
        layout.text_semantics.validate(changed)
        with self.assertRaisesRegex(AssertionError, "authenticated explicit prose"):
            prompt_fixtures(changed, historical)
        # Restoring the complete old prose/descriptor must not select a legacy
        # branch that overlooks a changed code card still owned by this edition.
        changed = copy.deepcopy(active)
        layout._blocks(changed)["block_0001_0001"]["text"] = layout._blocks(historical)["block_0001_0001"]["text"]
        marker = next(field for field in changed["textSemantics"]["fields"]
                      if (field["block"], field["path"]) == ("block_0001_0001", "/text"))
        original_marker = next(field for field in historical["textSemantics"]["fields"]
                               if (field["block"], field["path"]) == ("block_0001_0001", "/text"))
        marker.clear()
        marker.update(copy.deepcopy(original_marker))
        layout._blocks(changed)[PROMPT_INSTRUCTION_OWNER]["code"] = 'std::cout << "Cantidad falsa: ";'
        changed = resealed_edition_fixture(changed)
        with self.assertRaisesRegex(AssertionError, "authenticated explicit prose"):
            prompt_fixtures(changed, historical)
        for identity in (PROMPT_INSTRUCTION_OWNER, PROMPT_OUTPUT_OWNER):
            with self.subTest(missing_owner=identity):
                changed = copy.deepcopy(active)
                for page, row in zip(changed["pages"], changed["stateLayout"]["current"], strict=True):
                    if identity in row["blocks"]:
                        index = row["blocks"].index(identity)
                        del page["blocks"][index]
                        del row["blocks"][index]
                changed["textSemantics"]["fields"] = [field for field in changed["textSemantics"]["fields"]
                                                       if field["block"] != identity]
                changed = resealed_edition_fixture(changed)
                with self.assertRaisesRegex(AssertionError, "both authored split owners"):
                    prompt_fixtures(changed, historical)

    def test_active_prompt_rejects_broken_utf16_and_cannot_replace_the_archive(self):
        _, active, _, historical, _ = selected_prompt_documents()
        changed = copy.deepcopy(active)
        marker = next(field for field in changed["textSemantics"]["fields"]
                      if (field["block"], field["path"]) == (PROMPT_OUTPUT_OWNER, "/text"))
        marker["spans"][0]["start"] += 1
        with self.assertRaises((AssertionError, ValueError)):
            prompt_fixtures(changed, historical)
        changed = copy.deepcopy(historical)
        changed["stateLayout"]["retirementHistory"] = []
        with self.assertRaises((AssertionError, ValueError)):
            prompt_fixtures(active, changed)


class SpanishProseBoundariesTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.asset_bytes = ASSET.read_bytes()
        lesson = json.loads(cls.asset_bytes.decode("utf-8-sig"))
        block = lesson["pages"][5]["blocks"][3]
        if block["type"] != "multiple_choice" or block["correctIndex"] != 2:
            raise AssertionError("The current U2L3 choice identity changed")
        cls.current = block["explanation"]
        cls.u11_bytes = U11_ASSET.read_bytes()
        u11 = json.loads(cls.u11_bytes.decode("utf-8-sig"))
        cls.u11_source = u11["pages"][2]["blocks"][3]["text"]
        cls.u11_sentence = cls.u11_source.split(". ", 1)[0] + "."
        if "cada get válido consume un carácter y el intento final llega al fin." not in cls.u11_sentence:
            raise AssertionError("The current U11L14 p3b4 narration identity changed")
        cls.worker = Worker()

    @classmethod
    def tearDownClass(cls):
        cls.worker.close()
        if ASSET.read_bytes() != cls.asset_bytes:
            raise AssertionError("Narration QA changed the curricular asset")
        if U11_ASSET.read_bytes() != cls.u11_bytes:
            raise AssertionError("Narration QA changed the current U11L14 asset")

    def checked_plan(self, source, voice, context="auto", previous_prose_inference=False, text_semantics=None):
        previous = {"previousProseInference": True} if previous_prose_inference else {}
        if text_semantics is not None:
            previous["textSemantics"] = text_semantics
        plan = self.worker.ask("plan", text=source, readingContext=context, language="es", voice=voice, **previous)
        self.assertTrue(plan["segments"])
        cursor = 0
        for segment in plan["segments"]:
            start, end = segment["sourceStart"], segment["sourceEnd"]
            self.assertTrue(cursor <= start < end <= units(source))
            self.assertEqual(slice_units(source, start, end), segment["text"])
            cursor = end
            self.assertRegex(segment["key"], r"^[0-9a-f]{64}$")
            self.assertEqual(len(segment["spokenRanges"]), len(segment["displayedRanges"]))
            for spoken, displayed in zip(segment["spokenRanges"], segment["displayedRanges"]):
                self.assertTrue(0 <= spoken["start"] < spoken["end"] <= units(segment["spokenText"]))
                self.assertTrue(0 <= displayed["start"] < displayed["end"] <= units(segment["text"]))
            for word in ("definición", "actualización"):
                for match in re.finditer(word, segment["text"]):
                    first, last = units(segment["text"][:match.start()]), units(segment["text"][:match.end()])
                    pairs = [(s, d) for s, d in zip(segment["spokenRanges"], segment["displayedRanges"])
                             if owns_word(segment["text"], d, first, last)]
                    self.assertTrue(pairs, f"The complete visible word lost its UTF-16 owner: {word}; {segment}")
                    self.assertTrue(any(slice_units(segment["spokenText"], s["start"], s["end"]) == word
                                        for s, _ in pairs), f"A Spanish word became identifier fragments: {word}")
        rendered = " ".join(segment["preparedText"] for segment in plan["segments"])
        self.assertNotIn("`", rendered)
        for phrase in ("definición", "actualización", "lo reemplaza por uno"):
            if phrase in source:
                self.assertIn(phrase, rendered)
        self.assertNotIn("definici ó ene", rendered)
        self.assertNotIn("actualizaci ó ene", rendered)
        return plan

    def test_01_real_plans_preserve_prose_and_original_word_owners(self):
        self.assertEqual(ORIGINAL_SHA, hashlib.sha256(ORIGINAL.encode()).hexdigest())
        for voice in VOICES:
            for source in ("definición", "actualización", "✓ Correcto\n\n" + ORIGINAL,
                           "✓ Correcto\n\n" + self.current,
                           "𐐀 int turnos = 1 es una definición, no una actualización del objeto existente."):
                with self.subTest(voice=voice, source=source):
                    plan = self.checked_plan(source, voice)
                    self.assertEqual(plan["segments"], self.worker.ask("plan", text=source,
                                     language="es", voice=voice)["segments"])
                    if source.endswith(ORIGINAL):
                        speech = " ".join(part["spokenText"] for part in plan["segments"])
                        self.assertIn("incremento", speech)
                        self.assertEqual(2, len(re.findall(r"\basigna\b", speech)))
                    if source.endswith(self.current):
                        self.assertEqual(CURRENT_PREPARED, tuple(part["preparedText"] for part in plan["segments"]),
                                         "The already-correct reading and valid recording keys changed")

    def test_02_all_spanish_voices_real_pcm_timings_and_reused_recordings(self):
        recordings = {}
        for voice in VOICES:
            recordings[voice] = {}
            for name, source in (("definition", "definición"), ("update", "actualización"),
                                 ("original", "✓ Correcto\n\n" + ORIGINAL),
                                 ("current", "✓ Correcto\n\n" + self.current)):
                with self.subTest(voice=voice, name=name):
                    plan = self.checked_plan(source, voice)
                    rows = []
                    for part in plan["segments"]:
                        request = dict(text=part["text"], preparedText=part["preparedText"],
                                       language="es", voice=voice, threads=2, outputDirectory=str(OUT / "audio"))
                        result = self.worker.ask("prepare", **request)
                        self.assertEqual(part["key"], result["key"])
                        self.assertEqual(part["text"], result["sourceText"])
                        self.assertTrue(result["alignmentAvailable"])
                        self.assertEqual(24000, result["sampleRate"])
                        audio = Path(result["wav"])
                        with wave.open(str(audio), "rb") as wav:
                            self.assertEqual((1, 2, 24000), (wav.getnchannels(), wav.getsampwidth(), wav.getframerate()))
                            samples = struct.unpack("<" + "h" * wav.getnframes(), wav.readframes(wav.getnframes()))
                            self.assertGreater(max(map(abs, samples)), 50)
                            self.assertEqual(result["sampleCount"], len(samples))
                        self.assertGreater(result["durationMs"], 150)
                        for timing in result["timings"]:
                            self.assertTrue(0 <= timing["start"] < timing["end"] <= units(part["text"]))
                            self.assertTrue(0 <= timing["startMs"] < timing["endMs"] <= result["durationMs"])
                        for word in ("definición", "actualización"):
                            for match in re.finditer(word, part["text"]):
                                first, last = units(part["text"][:match.start()]), units(part["text"][:match.end()])
                                self.assertTrue(any(owns_word(part["text"], t, first, last) for t in result["timings"]),
                                                f"Actual model timing lost a complete word: {word}")
                        stamp = (audio.stat().st_mtime_ns, hashlib.sha256(audio.read_bytes()).hexdigest())
                        repeated = self.worker.ask("prepare", **request)
                        self.assertTrue(repeated["cacheHit"])
                        self.assertEqual(result["timings"], repeated["timings"])
                        self.assertEqual(stamp, (audio.stat().st_mtime_ns, hashlib.sha256(audio.read_bytes()).hexdigest()))
                        rows.append(dict(plan=part, recording=result, sha256=stamp[1]))
                    recordings[voice][name] = rows
        # Reopen the actual worker without deleting audio or changing source.
        self.worker.close()
        type(self).worker = Worker()
        for voice, cases in recordings.items():
            plan = self.checked_plan("✓ Correcto\n\n" + self.current, voice)
            for part, saved in zip(plan["segments"], cases["current"]):
                path = Path(saved["recording"]["wav"])
                stamp = path.stat().st_mtime_ns
                resumed = self.worker.ask("prepare", text=part["text"], preparedText=part["preparedText"],
                    language="es", voice=voice, threads=2, outputDirectory=str(OUT / "audio"))
                self.assertTrue(resumed["cacheHit"])
                self.assertEqual(saved["recording"]["timings"], resumed["timings"])
                self.assertEqual(saved["sha256"], hashlib.sha256(path.read_bytes()).hexdigest())
                self.assertEqual(stamp, path.stat().st_mtime_ns)
        OUT.mkdir(parents=True, exist_ok=True)
        (OUT / "recordings.json").write_text(json.dumps(recordings, ensure_ascii=False, indent=2), encoding="utf-8")
        print("PASS actual PCM/timings/cache/reopen for all three Spanish voices; human listening remains separate")

    def test_03_current_u11_conjunction_keeps_prose_and_named_letter_negatives(self):
        positive = (self.u11_source, self.u11_sentence, "𐐀 " + self.u11_sentence,
                    "El carácter y la línea permanecen.", "Cada carácter y un intento cuentan.",
                    "Cada letra y las palabras permanecen.", "Letras y palabras.",
                    "El texto contiene letras y palabras.", "La letra y la palabra permanecen.")
        # Preserve the exact preceding frontend for bookmark authentication.
        # Its inferred bare letters are historical pronunciations, rather than
        # requirements for newly authored unmarked prose. Explicit code/math
        # still reaches the canonical readers unchanged in either policy.
        negatives = {
            "La letra y es una consonante.": "La letra i griega es una consonante.",
            "La letra y la puedes escribir en minúscula.": "La letra i griega la puedes escribir en minúscula.",
            "El carácter y lo debemos escribir de nuevo.": "El carácter i griega lo debemos escribir de nuevo.",
            "El carácter Y es válido.": "El carácter i griega es válido.",
            "El carácter y: una letra.": "El carácter i griega: una letra.",
            "El carácter y y la letra z son distintos.": "El carácter i griega y la letra zeta son distintos.",
            "El carácter 'y' es válido.": "El carácter abre comilla simple i griega cierra comilla simple es válido.",
            "La variable `y` contiene 1.": "La variable i griega contiene 1.",
            "`int y = 1;` conserva la variable.": "int i griega asigna uno punto y coma conserva la variable.",
            "$y+1$ conserva la variable.": "ye más uno conserva la variable.",
            "El carácter y_el continúa.": "El carácter i griega guion bajo el continúa.",
            "La función f(y) conserva el carácter.": "La función f(y) conserva el carácter.",
            "Cada carácter (Stroustrup, 2024) conserva su posición.": "Cada carácter, Stroustrup, 2024 conserva su posición.",
            "El carácter y\n\nel intento permanece.": "El carácter i griega el intento permanece.",
            "El carácter y, el intento permanece.": "El carácter i griega, el intento permanece.",
        }
        for voice in VOICES:
            for source in positive:
                with self.subTest(voice=voice, source=source):
                    plan = self.checked_plan(source, voice)
                    spoken = " ".join(part["spokenText"] for part in plan["segments"])
                    self.assertNotIn("i griega", spoken)
                    if "carácter y el intento" in source:
                        self.assertIn("carácter y el intento", spoken)
                    for part in plan["segments"]:
                        for match in re.finditer(r"\by\b", part["text"]):
                            first, last = units(part["text"][:match.start()]), units(part["text"][:match.end()])
                            owners = [(s, d) for s, d in zip(part["spokenRanges"], part["displayedRanges"])
                                      if owns_word(part["text"], d, first, last)]
                            self.assertTrue(any(slice_units(part["spokenText"], s["start"], s["end"]) == "y"
                                                for s, _ in owners), "The conjunctive y lost its exact UTF-16 owner")
                    self.assertEqual(plan["segments"], self.worker.ask("plan", text=source,
                                     language="es", voice=voice)["segments"])
            for source, expected in negatives.items():
                with self.subTest(voice=voice, negative=source):
                    plan = self.checked_plan(source, voice, previous_prose_inference=True)
                    self.assertEqual(expected, " ".join(part["spokenText"] for part in plan["segments"]))

    def test_04_current_u11_conjunction_real_pcm_timings_and_cache_in_three_voices(self):
        recordings = {}
        for voice in VOICES:
            plan = self.checked_plan(self.u11_sentence, voice)
            rows = []
            self.assertNotIn("i griega", " ".join(part["spokenText"] for part in plan["segments"]))
            for part in plan["segments"]:
                request = dict(text=part["text"], preparedText=part["preparedText"], language="es", voice=voice,
                               threads=2, outputDirectory=str(OUT / "u11-conjunction-audio"))
                result = self.worker.ask("prepare", **request)
                self.assertEqual(part["key"], result["key"])
                self.assertEqual(part["text"], result["sourceText"])
                self.assertTrue(result["alignmentAvailable"])
                audio = Path(result["wav"])
                with wave.open(str(audio), "rb") as wav:
                    self.assertEqual((1, 2, 24000), (wav.getnchannels(), wav.getsampwidth(), wav.getframerate()))
                    samples = struct.unpack("<" + "h" * wav.getnframes(), wav.readframes(wav.getnframes()))
                    self.assertGreater(max(map(abs, samples)), 50)
                    self.assertEqual(result["sampleCount"], len(samples))
                self.assertGreater(result["durationMs"], 150)
                previous = 0
                for timing in result["timings"]:
                    self.assertTrue(0 <= timing["start"] < timing["end"] <= units(part["text"]))
                    self.assertTrue(previous <= timing["startMs"] < timing["endMs"] <= result["durationMs"])
                    previous = timing["startMs"]
                for word in ("carácter", "y", "intento", "fin"):
                    for match in re.finditer(r"\b" + word + r"\b", part["text"]):
                        first, last = units(part["text"][:match.start()]), units(part["text"][:match.end()])
                        self.assertTrue(any(owns_word(part["text"], timing, first, last) for timing in result["timings"]),
                                        f"Actual model timing lost the complete original word: {word}")
                stamp = (audio.stat().st_mtime_ns, hashlib.sha256(audio.read_bytes()).hexdigest())
                repeated = self.worker.ask("prepare", **request)
                self.assertTrue(repeated["cacheHit"])
                self.assertEqual(result["timings"], repeated["timings"])
                self.assertEqual(stamp, (audio.stat().st_mtime_ns, hashlib.sha256(audio.read_bytes()).hexdigest()))
                rows.append(dict(plan=part, recording=result, sha256=stamp[1]))
            recordings[voice] = rows
        OUT.mkdir(parents=True, exist_ok=True)
        (OUT / "u11-recordings.json").write_text(json.dumps(recordings, ensure_ascii=False, indent=2), encoding="utf-8")
        print("PASS current U11L14 original conjunction: actual three-voice PCM, UTF-16 word timing and cache reuse; listening remains separate")

    def test_05_current_output_prompt_colon_and_conjunction_pcm_context_and_cache(self):
        asset, lesson, historical_asset, historical_lesson, originals = selected_prompt_documents()
        # Select by immutable owner and authenticated meaning, then translate
        # every UTF-16 span with its excerpt. The current C++ instruction and
        # the console text have separate reading contracts; keep the archived
        # sentence as an independent fixture instead of imposing it on today.
        sources = prompt_fixtures(lesson, historical_lesson)
        folder = OUT / f"output-colon-conjunction-{time.time_ns()}"
        folder.mkdir(parents=True, exist_ok=False)
        evidence = {"asset": str(asset), "assetSha256": hashlib.sha256(originals[asset]).hexdigest(),
                    "courseEdition": lesson["courseEdition"],
                    "historicalAsset": str(historical_asset),
                    "historicalAssetSha256": hashlib.sha256(originals[historical_asset]).hexdigest(),
                    "inputDocuments": {str(path): hashlib.sha256(data).hexdigest() for path, data in originals.items()},
                    "sources": sources, "voices": {}}
        retained = []
        try:
            for voice in VOICES:
                evidence["voices"][voice] = {}
                for name, fixture in sources.items():
                    source, meaning = fixture["text"], fixture["textSemantics"]
                    plans = {context: self.checked_plan(source, voice, context, text_semantics=meaning)
                             for context in ("auto", "cpp")}
                    self.assertEqual(plans["auto"]["segments"], plans["cpp"]["segments"],
                                     "Puzzle context changed explicitly authored prompt meaning")
                    speech = " ".join(part["spokenText"] for part in plans["auto"]["segments"])
                    if name == "current-instruction":
                        self.assertIn("punto y coma", speech)
                        self.assertIn("Cantidad", speech)
                    else:
                        self.assertNotIn("dos puntos", speech)
                    rows = []
                    for part in plans["auto"]["segments"]:
                        self.assertNotIn("i griega", part["spokenText"])
                        words = ("letras", "y", "palabras") if name == "conjunction" else ("Cantidad",)
                        for word in words:
                            for match in re.finditer(r"\b" + word + r"\b", part["text"]):
                                first, last = units(part["text"][:match.start()]), units(part["text"][:match.end()])
                                self.assertTrue(any(owns_word(part["text"], shown, first, last)
                                    and slice_units(part["spokenText"], spoken["start"], spoken["end"]).strip(" ,.;:!?") == word
                                    for spoken, shown in zip(part["spokenRanges"], part["displayedRanges"])),
                                    f"A complete original output/prose word lost its exact UTF-16 speech owner: {word}")
                        request = dict(text=part["text"], preparedText=part["preparedText"], language="es", voice=voice,
                                       readingContext="auto", threads=2, outputDirectory=str(folder / "audio"))
                        result = self.worker.ask("prepare", **request)
                        self.assertEqual(part["key"], result["key"])
                        self.assertEqual(part["text"], result["sourceText"])
                        self.assertTrue(result["alignmentAvailable"])
                        audio = Path(result["wav"])
                        with wave.open(str(audio), "rb") as wav:
                            self.assertEqual((1, 2, 24000), (wav.getnchannels(), wav.getsampwidth(), wav.getframerate()))
                            samples = struct.unpack("<" + "h" * wav.getnframes(), wav.readframes(wav.getnframes()))
                            self.assertGreater(max(map(abs, samples)), 50)
                            self.assertEqual(result["sampleCount"], len(samples))
                        self.assertGreater(result["durationMs"], 150)
                        previous = 0
                        for timing in result["timings"]:
                            self.assertTrue(0 <= timing["start"] < timing["end"] <= units(part["text"]))
                            self.assertTrue(previous <= timing["startMs"] < timing["endMs"] <= result["durationMs"])
                            previous = timing["startMs"]
                        for word in words:
                            for match in re.finditer(r"\b" + word + r"\b", part["text"]):
                                first, last = units(part["text"][:match.start()]), units(part["text"][:match.end()])
                                self.assertTrue(any(owns_word(part["text"], timing, first, last) for timing in result["timings"]),
                                                f"Real PCM alignment lost a complete original word: {word}")
                        stamp = (audio.stat().st_mtime_ns, hashlib.sha256(audio.read_bytes()).hexdigest())
                        repeated = self.worker.ask("prepare", **(request | {"readingContext": "cpp"}))
                        self.assertTrue(repeated["cacheHit"])
                        self.assertEqual(result["timings"], repeated["timings"])
                        self.assertEqual(stamp, (audio.stat().st_mtime_ns, hashlib.sha256(audio.read_bytes()).hexdigest()))
                        rows.append(dict(plan=part, recording=result, sha256=stamp[1], mtimeNs=stamp[0]))
                        retained.append((request, result, stamp))
                    evidence["voices"][voice][name] = rows
            self.worker.close()
            type(self).worker = Worker()
            for voice in VOICES:
                for fixture in sources.values():
                    self.checked_plan(fixture["text"], voice, "cpp", text_semantics=fixture["textSemantics"])
            for request, result, stamp in retained:
                repeated = self.worker.ask("prepare", **(request | {"readingContext": "cpp"}))
                self.assertTrue(repeated["cacheHit"])
                self.assertEqual(result["timings"], repeated["timings"])
                audio = Path(result["wav"])
                self.assertEqual(stamp, (audio.stat().st_mtime_ns, hashlib.sha256(audio.read_bytes()).hexdigest()))
        finally:
            for path, original in originals.items():
                self.assertEqual(original, path.read_bytes(), "Narration QA changed a selected or historical source")
            (folder / "recordings.json").write_text(json.dumps(evidence, ensure_ascii=False, indent=2), encoding="utf-8")
        print(f"PASS current prompt/output colon and conjunctive y: three-voice real PCM, auto/cpp parity, exact UTF-16 owners, cache reuse/reopen; evidence {folder / 'recordings.json'}; human listening remains separate")


if __name__ == "__main__":
    unittest.main()

