"""Real Windows plans, provenance and Kokoro audio at C++/Spanish boundaries.

Fixtures never edit curriculum, user state or recordings. Generated samples are
kept for human listening; PCM/timing checks do not assert pronunciation quality.
"""
from __future__ import annotations

import hashlib
import json
from pathlib import Path
import re
import struct
import time
import unittest
import wave

from test_worker import ROOT, Worker


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

    def checked_plan(self, source, voice, context="auto"):
        plan = self.worker.ask("plan", text=source, readingContext=context, language="es", voice=voice)
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
        # Explicit code/math reaches the established canonical readers before
        # this Windows-only prose rule. Bare named letters retain that reading,
        # including a second y that really is the conjunction after their name.
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
                    plan = self.checked_plan(source, voice)
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
        asset = ROOT / "app/src/main/assets/content/es/programming/cpp_fundamentals/objetos_y_estado/lesson_leer_una_entrada_conocida.json"
        original = asset.read_bytes()
        lesson = json.loads(original.decode("utf-8-sig"))
        prompts = [block["text"] for page in lesson["pages"] for block in page["blocks"]
                   if block.get("type") == "paragraph" and block.get("text", "").startswith("Un prompt es un mensaje")]
        self.assertEqual(1, len(prompts), "The current prompt narration owner changed")
        # Retain the complete local sentence, including its explanation after
        # the sample. Tint/backticks alone never establish an output role.
        sentence = "Por ejemplo, " + prompts[0].split("Por ejemplo, ", 1)[1].split(". ", 1)[0] + "."
        self.assertEqual("Por ejemplo, `Cantidad: ` puede avisar de que se recibirá una cantidad.", sentence)
        sources = {"current-prompt": sentence, "conjunction": "El texto contiene letras y palabras."}
        folder = OUT / f"output-colon-conjunction-{time.time_ns()}"
        folder.mkdir(parents=True, exist_ok=False)
        evidence = {"asset": str(asset), "assetSha256": hashlib.sha256(original).hexdigest(), "sources": sources, "voices": {}}
        retained = []
        try:
            for voice in VOICES:
                evidence["voices"][voice] = {}
                for name, source in sources.items():
                    plans = {context: self.checked_plan(source, voice, context) for context in ("auto", "cpp")}
                    self.assertEqual(plans["auto"]["segments"], plans["cpp"]["segments"],
                                     "A colored C++ puzzle context changed ordinary output/prose speech")
                    rows = []
                    for part in plans["auto"]["segments"]:
                        self.assertNotIn("dos puntos", part["spokenText"])
                        self.assertNotIn("i griega", part["spokenText"])
                        words = ("Cantidad",) if name == "current-prompt" else ("letras", "y", "palabras")
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
                for source in sources.values():
                    self.checked_plan(source, voice, "cpp")
            for request, result, stamp in retained:
                repeated = self.worker.ask("prepare", **(request | {"readingContext": "cpp"}))
                self.assertTrue(repeated["cacheHit"])
                self.assertEqual(result["timings"], repeated["timings"])
                audio = Path(result["wav"])
                self.assertEqual(stamp, (audio.stat().st_mtime_ns, hashlib.sha256(audio.read_bytes()).hexdigest()))
        finally:
            self.assertEqual(original, asset.read_bytes(), "Narration QA changed the current curricular prompt")
            (folder / "recordings.json").write_text(json.dumps(evidence, ensure_ascii=False, indent=2), encoding="utf-8")
        print(f"PASS current prompt/output colon and conjunctive y: three-voice real PCM, auto/cpp parity, exact UTF-16 owners, cache reuse/reopen; evidence {folder / 'recordings.json'}; human listening remains separate")


if __name__ == "__main__":
    unittest.main()
