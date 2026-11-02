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
import unittest
import wave

from test_worker import ROOT, Worker


ASSET = ROOT / "app/src/main/assets/content/es/programming/cpp_fundamentals/objetos_y_estado/lesson_estado_asignacion_y_copia.json"
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
        cls.worker = Worker()

    @classmethod
    def tearDownClass(cls):
        cls.worker.close()
        if ASSET.read_bytes() != cls.asset_bytes:
            raise AssertionError("Narration QA changed the curricular asset")

    def checked_plan(self, source, voice):
        plan = self.worker.ask("plan", text=source, language="es", voice=voice)
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


if __name__ == "__main__":
    unittest.main()
