"""Typed Windows C++ puzzle speech; original assets and old audio remain intact."""
from __future__ import annotations

import copy
import hashlib
import json
from pathlib import Path
import re
import struct
import unittest
import wave

from test_worker import ROOT, Worker


ASSET = ROOT / "app/src/main/assets/content/es/programming/cpp_fundamentals/texto_interpretacion_y_flujos/lesson_terminos_prioridad_y_division_segura.json"
OUT = ROOT / "out/qa/cpp-numeric-context"
VOICES = ("ef_dora", "em_alex", "em_santa")
ATOMIC_FRACTION = "La fracción -0,5/2 conserva su valor."


def units(text):
    return len(text.encode("utf-16-le")) // 2


def piece(text, first, last):
    return text.encode("utf-16-le")[first * 2:last * 2].decode("utf-16-le")


class CppNumericContextTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.original = ASSET.read_bytes()
        cls.lesson = json.loads(cls.original.decode("utf-8-sig"))
        blocks = cls.lesson["pages"][6]["blocks"]
        cls.sources = (blocks[1]["explanation"], blocks[2]["question"], blocks[2]["options"][0])
        if "11/2" not in cls.sources[0] or "11/2*2" not in cls.sources[1] or "11/2=5" not in cls.sources[2]:
            raise AssertionError("The actual U11L12 p7 sources changed")
        cls.worker = Worker()

    @classmethod
    def tearDownClass(cls):
        cls.worker.close()
        if ASSET.read_bytes() != cls.original:
            raise AssertionError("Numeric narration QA changed the authored asset")

    def plan(self, text, context="auto", voice="ef_dora", mode="auto"):
        result = self.worker.ask("plan", text=text, readingContext=context, mathMode=mode,
                                 language="es", voice=voice)["segments"]
        cursor = 0
        # This existing complex AST fraction expands across a spoken semicolon.
        # Its two audio segments intentionally share the one original formula,
        # including its following space. Assert that exact ownership contract;
        # ordinary source segments and all typed C++ operators stay disjoint.
        atomic = text == ATOMIC_FRACTION and mode == "auto"
        if atomic:
            self.assertEqual([(0, 19), (12, 37)], [(p["sourceStart"], p["sourceEnd"]) for p in result])
            self.assertEqual(["La fracción fracción con numerador, menos cero coma cinco;",
                              "y denominador, dos, fin de fracción conserva su valor."],
                             [p["spokenText"] for p in result])
            self.assertEqual([(0, 3), (3, 12)] + [(12, 19)] * 7,
                             [(d["start"], d["end"]) for d in result[0]["displayedRanges"]])
            self.assertEqual([(0, 7)] * 6 + [(7, 16), (16, 19), (19, 25)],
                             [(d["start"], d["end"]) for d in result[1]["displayedRanges"]])
        for part in result:
            first, last = part["sourceStart"], part["sourceEnd"]
            self.assertTrue((0 if atomic else cursor) <= first < last <= units(text))
            self.assertEqual(part["text"], piece(text, first, last))
            self.assertEqual(len(part["spokenRanges"]), len(part["displayedRanges"]))
            self.assertTrue(part["spokenRanges"], "The real normalizer lost its UTF-16 word mapping")
            for spoken, shown in zip(part["spokenRanges"], part["displayedRanges"]):
                self.assertTrue(0 <= spoken["start"] < spoken["end"] <= units(part["spokenText"]))
                self.assertTrue(0 <= shown["start"] < shown["end"] <= units(part["text"]))
            cursor = last
        return result

    @staticmethod
    def speech(parts):
        return " ".join(part["spokenText"] for part in parts)

    def test_01_actual_typed_puzzle_sources_use_numeric_code_and_exact_operators(self):
        for source in self.sources:
            for text in (source, "𐐀 " + source):
                with self.subTest(source=text):
                    parts = self.plan(text, "cpp")
                    spoken = self.speech(parts)
                    self.assertIn("once barra dos", spoken)
                    self.assertNotIn("medios", spoken)
                    self.assertNotIn("=", spoken)
                    if "=5" in text:
                        self.assertIn("igual a cinco", spoken)
                        self.assertNotIn("asigna", spoken)
                    for part in parts:
                        for match in re.finditer(r"[/=*]", part["text"]):
                            first = units(part["text"][:match.start()])
                            expected = {"/": "barra", "*": "por", "=": "igual"}[match.group()]
                            self.assertTrue(any(piece(part["spokenText"], s["start"], s["end"]) == expected
                                and d["start"] == first and d["end"] == first + 1
                                for s, d in zip(part["spokenRanges"], part["displayedRanges"])),
                                f"The original operator lost its own UTF-16 glyph: {match.group()}")
                    self.assertEqual(parts, self.plan(text, "cpp"))
        self.assertEqual("veinticuatro barra cuatro barra dos", self.speech(self.plan("24/4/2", "cpp")))
        self.assertIn("comparación de igualdad", self.speech(self.plan("11/2==5", "cpp")))
        self.assertIn("asigna once barra dos", self.speech(self.plan("int cociente = 11/2;", "cpp")))
        for incomplete in ("11/(2+3", "11/((2+3)"):
            self.assertTrue(self.speech(self.plan(incomplete, "cpp")).startswith("11/("),
                            "A rejected group donated an operator to an earlier numeric prefix")
        self.assertEqual("once barra abre paréntesis dos más tres cierra paréntesis",
                         self.speech(self.plan("11/(2+3)", "cpp")))
        self.assertEqual("veinticuatro barra abre paréntesis cuatro barra dos cierra paréntesis",
                         self.speech(self.plan("24/(4/2)", "cpp")))

    def test_02_auto_math_explicit_modes_prose_paths_dates_and_literals_stay_unchanged(self):
        sources = ("11/2", "La fracción 11/2 conserva su valor.", "La fracción $11/2$ conserva su valor.",
                   r"La fracción \(11/2\) conserva su valor.", "La fecha 24/4/2 queda escrita.",
                   "La fracción 0,5/2 conserva su valor.", "La fracción 10,25/2 conserva su valor.",
                   ATOMIC_FRACTION, "La fracción -11/2 conserva su valor.",
                   "La fracción −11/2 conserva su valor.", "La fracción - 11/2 conserva su valor.",
                   "La fracción 0.5/2 conserva su valor.",
                   "El día 24/4/2 queda escrito.", r"La ruta C:\Work\11/2 sigue disponible.",
                   "Consulta https://example.org/11/2 y continúa.", 'El literal "11/2" queda escrito.',
                   "`11/2*2` conserva la expresión.", "El término conserva definición y actualización.")
        for source in sources:
            with self.subTest(source=source):
                auto = self.plan(source)
                if source != "11/2":
                    self.assertEqual(auto, self.plan(source, "cpp"))
                if source in ("11/2", "La fracción 11/2 conserva su valor.", "La fracción $11/2$ conserva su valor.",
                              r"La fracción \(11/2\) conserva su valor."):
                    self.assertIn("once medios", self.speech(auto))
        self.assertEqual("cero punto cinco barra dos", self.speech(self.plan("0.5/2", "cpp")))
        self.assertIn("barra", self.speech(self.plan("-0.5/2", "cpp")))
        for mode in ("text", "inline", "block"):
            for source in ("11/2", "11/2*2"):
                with self.subTest(mode=mode, source=source):
                    self.assertEqual(self.plan(source, mode=mode), self.plan(source, "cpp", mode=mode))
        with self.assertRaisesRegex(AssertionError, "Invalid reading context"):
            self.worker.ask("plan", text="11/2", readingContext="all-code", language="es", voice="ef_dora")
        with self.assertRaisesRegex(AssertionError, "Invalid mathematical reading mode"):
            self.worker.ask("plan", text="11/2", mathMode="cpp", language="es", voice="ef_dora")
        for context in ("auto", "cpp"):
            for mode in ("", " ", "cpp", "bogus"):
                with self.subTest(context=context, invalid_mode=mode):
                    with self.assertRaisesRegex(AssertionError, "Invalid mathematical reading mode"):
                        self.worker.ask("plan", text="11/2", readingContext=context, mathMode=mode,
                                        language="es", voice="ef_dora")
                    for kind in ("paragraph", "multiple_choice"):
                        lesson = {"pages": [{"blocks": [dict(type=kind, text="11/2", question="11/2", mathMode=mode)]}]}
                        with self.assertRaisesRegex(AssertionError, "Invalid mathematical reading mode"):
                            self.worker.ask("curriculum_plan", lessonJson=json.dumps(lesson), page=0,
                                            readingContext=context, language="es", voice="ef_dora")
        self.assertIn("once medios", self.speech(self.plan("11/2")), "Rejected requests terminated the worker")

    def test_03_background_and_foreground_share_keys_only_for_public_typed_blocks(self):
        for voice in VOICES:
            background = self.worker.ask("curriculum_plan", lessonJson=self.original.decode("utf-8-sig"), page=6,
                readingContext="cpp", language="es", voice=voice)["segments"]
            keys = {part["key"] for part in background}
            for source in self.sources:
                expected = self.plan(source, "cpp", voice)
                self.assertTrue({part["key"] for part in expected}.issubset(keys),
                                "Background missed the actual typed question/option/success recording")
        types = ("multiple_choice", "fill_code", "inline_fill_code", "output_prediction", "order_code", "drag_fill_code")
        text = "La expresión 24/4/2 conserva el orden."
        cpp_keys = {part["key"] for part in self.plan(text, "cpp")}
        auto_keys = {part["key"] for part in self.plan(text)}
        self.assertTrue(cpp_keys.isdisjoint(auto_keys))
        for kind in types:
            # Ordering has its own closed cpp/cmake authoring contract. Missing
            # language is its legitimate default; an explicit empty string or
            # Python would be an invalid fixture, not another supported target.
            languages = (None, "cpp", "c++", "cmake", "CPP", "C++") if kind == "order_code" else ("", "cpp", "c++", "python", "CPP", "C++")
            for language in languages:
                for mode in ("auto", "text", "inline", "block"):
                    block = dict(type=kind, mathMode=mode, question=text, options=[text],
                                 explanation=text, validator={"expected": "secretvalidatorqqq"})
                    if language is not None:
                        block["language"] = language
                    lesson = {"title": text, "pages": [{"blocks": [dict(type="paragraph", text=text), block,
                              dict(type="math_puzzle", prompt=text, mathMode="auto")]}]}
                    saved = copy.deepcopy(lesson)
                    parts = self.worker.ask("curriculum_plan", lessonJson=json.dumps(lesson), page=0,
                        readingContext="cpp", language="es", voice="ef_dora")["segments"]
                    keys = {part["key"] for part in parts}
                    self.assertEqual(saved, lesson)
                    self.assertNotIn("secretvalidatorqqq", json.dumps(parts))
                    self.assertTrue(auto_keys.issubset(keys), "Title/prose/math puzzle inherited a C++ puzzle's context")
                    expected = {p["key"] for p in self.plan(text, "cpp" if language in (None, "", "cpp", "c++") else "auto", mode=mode)}
                    self.assertTrue(expected.issubset(keys))
                    if language not in (None, "", "cpp", "c++") or mode != "auto":
                        self.assertTrue(cpp_keys.isdisjoint(keys))

    def prepare(self, part, voice):
        request = dict(text=part["text"], preparedText=part["preparedText"], language="es", voice=voice,
                       threads=2, outputDirectory=str(OUT / "audio"))
        result = self.worker.ask("prepare", **request)
        self.assertEqual(part["key"], result["key"])
        self.assertEqual(part["text"], result["sourceText"])
        self.assertTrue(result["alignmentAvailable"])
        audio = Path(result["wav"])
        with wave.open(str(audio), "rb") as wav:
            self.assertEqual((1, 2, 24000), (wav.getnchannels(), wav.getsampwidth(), wav.getframerate()))
            samples = struct.unpack("<" + "h" * wav.getnframes(), wav.readframes(wav.getnframes()))
            self.assertGreater(max(map(abs, samples)), 50)
        self.assertTrue(result["timings"])
        for item in result["timings"]:
            self.assertTrue(0 <= item["start"] < item["end"] <= units(part["text"]))
            self.assertTrue(0 <= item["startMs"] < item["endMs"] <= result["durationMs"])
        stamp = (audio.stat().st_mtime_ns, hashlib.sha256(audio.read_bytes()).hexdigest())
        repeated = self.worker.ask("prepare", **request)
        self.assertTrue(repeated["cacheHit"])
        self.assertEqual(result["timings"], repeated["timings"])
        self.assertEqual(stamp, (audio.stat().st_mtime_ns, hashlib.sha256(audio.read_bytes()).hexdigest()))
        return result, stamp

    def test_04_three_voice_pcm_and_cache_preserve_preceding_fraction_recordings(self):
        samples = {}
        for voice in VOICES:
            old = self.plan(self.sources[1], voice=voice)
            current = self.plan(self.sources[1], "cpp", voice)
            self.assertIn("medios", self.speech(old))
            self.assertNotIn("medios", self.speech(current))
            self.assertNotEqual(old[0]["key"], current[0]["key"])
            original, stamp = self.prepare(old[0], voice)
            fresh, _ = self.prepare(current[0], voice)
            fallback = self.worker.ask("prepare", text=self.sources[1], readingContext="cpp", mathMode="auto",
                language="es", voice=voice, threads=2, outputDirectory=str(OUT / "audio"))
            self.assertTrue(fallback["cacheHit"])
            self.assertEqual(fresh["key"], fallback["key"])
            self.assertEqual(fresh["timings"], fallback["timings"])
            # Reopening a previous plan still reuses its exact compatible WAV;
            # a new interpretation creates a separate key instead of deleting it.
            self.plan(self.sources[1], voice=voice)
            restored, again = self.prepare(old[0], voice)
            self.assertEqual(stamp, again)
            self.assertEqual(original["timings"], restored["timings"])
            samples[voice] = dict(previous=original, current=fresh)
        OUT.mkdir(parents=True, exist_ok=True)
        (OUT / "recordings.json").write_text(json.dumps(samples, ensure_ascii=False, indent=2), encoding="utf-8")
        print("PASS actual U11L12 typed-puzzle PCM/timings/cache in three Spanish voices; human listening remains separate")


if __name__ == "__main__":
    unittest.main()
