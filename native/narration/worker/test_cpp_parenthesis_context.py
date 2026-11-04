"""Real Windows C++ token mentions; no curricular edits or listening claims."""
from __future__ import annotations

import hashlib
import json
from pathlib import Path
import re
import struct
import unicodedata
import unittest
import wave

from test_worker import ROOT, Worker


ASSET = ROOT / "app/src/main/assets/content/es/programming/cpp_fundamentals/texto_interpretacion_y_flujos/lesson_signos_unarios_parentesis_y_progreso_recursivo.json"
OUT = ROOT / "out/qa/cpp-parenthesis-context"
VOICES = ("ef_dora", "em_alex", "em_santa")
CASES = (
    (4, 1, "block_0003_0001", "explanation", ")", "cierra paréntesis",
     "La unidad agrupada obtiene 7 de su expresión interior y consume ). El término exterior utiliza esa unidad para multiplicar por 3."),
    (6, 2, "block_0006_0002", "explanation", ")", "cierra paréntesis",
     "La expresión interior obtiene 6 y conserva ;. La unidad agrupada esperaba ) y rechaza la estructura, conservando el token inesperado para una política posterior."),
    (7, 1, "block_0007_0001", "question", "(", "abre paréntesis",
     "Una versión devuelve el mismo ( que acaba de obtener y vuelve a pedir la expresión sin consumirlo ni aumentar un límite. ¿Qué defecto tiene?"),
)


def units(text):
    return len(text.encode("utf-16-le")) // 2


def piece(text, first, last):
    return text.encode("utf-16-le")[first * 2:last * 2].decode("utf-16-le")


def normalized(text):
    return re.sub(r" +([,.;:!?])", r"\1", re.sub(r"\s+", " ", text).strip())


def owns_symbol(text, owner, symbol):
    if symbol not in text:
        return False
    first = units(text[:text.index(symbol)])
    last = first + units(symbol)
    if owner["start"] > first or owner["end"] < last:
        return False
    extra = piece(text, owner["start"], first) + piece(text, last, owner["end"])
    # Canonical owners may retain inter-word whitespace/punctuation. They
    # must own the full glyph without absorbing another letter or identifier.
    return all(char != "_" and (char.isspace() or unicodedata.category(char).startswith("P")) for char in extra)


class CppParenthesisContextTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.original = ASSET.read_bytes()
        cls.lesson = json.loads(cls.original.decode("utf-8-sig"))
        for page, block, identity, field, symbol, name, source in CASES:
            if (cls.lesson["stateLayout"]["current"][page]["blocks"][block] != identity
                    or cls.lesson["pages"][page]["blocks"][block][field] != source
                    or source.count(symbol) != 1):
                raise AssertionError("The actual U11L13 parenthesis source/identity changed")
        cls.worker = Worker()

    @classmethod
    def tearDownClass(cls):
        cls.worker.close()
        if ASSET.read_bytes() != cls.original:
            raise AssertionError("Parenthesis speech QA modified the authored asset")

    def plan(self, source, context="auto", voice="ef_dora", mode="auto"):
        parts = self.worker.ask("plan", text=source, readingContext=context, mathMode=mode,
                                language="es", voice=voice)["segments"]
        cursor = 0
        for part in parts:
            first, last = part["sourceStart"], part["sourceEnd"]
            self.assertTrue(cursor <= first < last <= units(source))
            self.assertEqual(part["text"], piece(source, first, last))
            self.assertEqual(len(part["spokenRanges"]), len(part["displayedRanges"]))
            for spoken, shown in zip(part["spokenRanges"], part["displayedRanges"]):
                self.assertTrue(0 <= spoken["start"] < spoken["end"] <= units(part["spokenText"]))
                self.assertTrue(0 <= shown["start"] < shown["end"] <= units(part["text"]))
            cursor = last
        return parts

    @staticmethod
    def speech(parts):
        return normalized(" ".join(part["spokenText"] for part in parts))

    def test_01_actual_cpp_mentions_name_the_original_parenthesis_glyph(self):
        for voice in VOICES:
            for _, _, identity, _, symbol, name, source in CASES:
                for text in (source, "𐐀 " + source):
                    with self.subTest(voice=voice, identity=identity, supplementary=text != source):
                        old = self.plan(text, voice=voice)
                        current = self.plan(text, "cpp", voice)
                        self.assertIn(symbol, self.speech(old))
                        self.assertEqual(normalized(self.speech(old).replace(symbol, name)), self.speech(current))
                        self.assertNotIn(symbol, self.speech(current))
                        for word in name.split():
                            self.assertTrue(any(piece(part["spokenText"], s["start"], s["end"]) == word
                                and owns_symbol(part["text"], d, symbol)
                                for part in current for s, d in zip(part["spokenRanges"], part["displayedRanges"])),
                                "The lexical parenthesis word lost its exact original UTF-16 glyph")

    def test_02_only_isolated_cpp_token_mentions_change(self):
        untouched = (
            "Texto (Stroustrup, 2024) conserva su referencia.",
            "El mismo (provisional) sigue disponible.",
            "El lector consume (un carácter) y continúa.",
            "El lector obtiene ( una respuesta opcional ) al terminar.",
            "El lector consume ( una respuesta opcional ) al terminar.",
            "El lector esperaba ( una respuesta opcional ) al terminar.",
            "El texto ) continúa.", "La nota ( sigue abierta.",
            "El token(5) termina.", "La función f(a, b) termina.",
            "El carácter `)` pertenece a la entrada.", "El carácter '(' pertenece a la entrada.",
            "Consulta https://example.org/(a) y continúa.",
            "La fórmula \\(x + (y + 2)\\) conserva su agrupación.",
        )
        for source in untouched:
            with self.subTest(source=source):
                self.assertEqual(self.plan(source), self.plan(source, "cpp"))
        for _, _, _, _, _, _, source in CASES:
            for mode in ("text", "inline", "block"):
                with self.subTest(source=source, mode=mode):
                    self.assertEqual(self.plan(source, mode=mode), self.plan(source, "cpp", mode=mode))
        for mode in ("inline", "block"):
            for source in ("(3+4)*2", "(x + y) / 2", "La fórmula (x+2) conserva la agrupación."):
                with self.subTest(source=source, mode=mode):
                    self.assertEqual(self.plan(source, mode=mode), self.plan(source, "cpp", mode=mode))
        for source, symbol, name in (("Se esperaba ).", ")", "cierra paréntesis"),
                ("El token ( inicia la agrupación.", "(", "abre paréntesis"),
                ("El lector conserva ) para la llamada exterior.", ")", "cierra paréntesis")):
            with self.subTest(source=source):
                old = self.speech(self.plan(source))
                self.assertIn(symbol, old)
                self.assertEqual(normalized(old.replace(symbol, name)), self.speech(self.plan(source, "cpp")))

    def test_03_actual_background_and_foreground_use_the_same_recording_keys(self):
        for voice in VOICES:
            for page, _, identity, _, symbol, _, source in CASES:
                with self.subTest(voice=voice, identity=identity):
                    background = self.worker.ask("curriculum_plan", lessonJson=self.original.decode("utf-8-sig"),
                        page=page, readingContext="cpp", language="es", voice=voice, uiLanguage="es")["segments"]
                    current = self.plan(source, "cpp", voice)
                    old = self.plan(source, voice=voice)
                    keys = {part["key"] for part in background}
                    self.assertTrue({part["key"] for part in current}.issubset(keys))
                    changed = {part["key"] for part in old if symbol in part["text"]}
                    self.assertTrue(changed.isdisjoint(keys), "Background retained a superseded literal-symbol recording")
                    for part in current:
                        self.assertTrue(any(item["key"] == part["key"] and item["preparedText"] == part["preparedText"]
                                            for item in background), "Background normalization diverged from the visible source")

    def prepare(self, part, voice, symbol=None):
        request = dict(text=part["text"], preparedText=part["preparedText"], language="es", voice=voice,
                       threads=2, outputDirectory=str(OUT / "audio"))
        result = self.worker.ask("prepare", **request)
        self.assertEqual((part["key"], part["text"]), (result["key"], result["sourceText"]))
        self.assertTrue(result["alignmentAvailable"])
        audio = Path(result["wav"])
        with wave.open(str(audio), "rb") as wav:
            self.assertEqual((1, 2, 24000), (wav.getnchannels(), wav.getsampwidth(), wav.getframerate()))
            self.assertEqual(result["sampleCount"], wav.getnframes())
            samples = struct.unpack("<" + "h" * wav.getnframes(), wav.readframes(wav.getnframes()))
            self.assertGreater(max(map(abs, samples)), 50)
        self.assertGreater(result["durationMs"], 150)
        self.assertTrue(result["timings"])
        for timing in result["timings"]:
            self.assertTrue(0 <= timing["start"] < timing["end"] <= units(part["text"]))
            self.assertTrue(0 <= timing["startMs"] < timing["endMs"] <= result["durationMs"])
        if symbol is not None:
            owned = [item for item in result["timings"] if owns_symbol(part["text"], item, symbol)]
            self.assertGreaterEqual(len(owned), 2, "Both lexical parenthesis words must align to their one original UTF-16 glyph")
        stamp = (audio.stat().st_mtime_ns, hashlib.sha256(audio.read_bytes()).hexdigest())
        repeated = self.worker.ask("prepare", **request)
        self.assertTrue(repeated["cacheHit"])
        self.assertEqual(result["timings"], repeated["timings"])
        self.assertEqual(stamp, (audio.stat().st_mtime_ns, hashlib.sha256(audio.read_bytes()).hexdigest()))
        return result, stamp

    def test_04_three_voice_pcm_retains_previous_and_unaffected_recordings_across_worker_reopen(self):
        preserved = []
        evidence = {}
        for voice in VOICES:
            evidence[voice] = {}
            for _, _, identity, _, symbol, _, source in CASES:
                with self.subTest(voice=voice, identity=identity):
                    old = self.plan(source, voice=voice)
                    current = self.plan(source, "cpp", voice)
                    old_by_source = {part["text"]: part for part in old}
                    self.assertEqual(set(old_by_source), {part["text"] for part in current})
                    previous = {}
                    for part in old:
                        audio, stamp = self.prepare(part, voice)
                        previous[part["key"]] = (audio, stamp)
                        preserved.append((source, "auto", voice, part, audio, stamp))
                    rows = []
                    for part in current:
                        preceding = old_by_source[part["text"]]
                        affected = symbol in part["text"]
                        if affected:
                            self.assertNotEqual(preceding["key"], part["key"])
                        else:
                            self.assertEqual(preceding["preparedText"], part["preparedText"])
                            self.assertEqual(preceding["key"], part["key"])
                        audio, stamp = self.prepare(part, voice, symbol if affected else None)
                        if not affected:
                            self.assertTrue(audio["cacheHit"])
                            self.assertEqual(previous[part["key"]][1], stamp)
                        preserved.append((source, "cpp", voice, part, audio, stamp))
                        rows.append(dict(plan=part, recording=audio, sha256=stamp[1]))
                    evidence[voice][identity] = rows
        self.worker.close()
        type(self).worker = Worker()
        for source, context, voice, part, original, stamp in preserved:
            with self.subTest(reopened_voice=voice, context=context, key=part["key"]):
                self.assertIn(part, self.plan(source, context, voice))
                restored, again = self.prepare(part, voice)
                self.assertTrue(restored["cacheHit"])
                self.assertEqual(stamp, again)
                self.assertEqual(original["timings"], restored["timings"])
        OUT.mkdir(parents=True, exist_ok=True)
        (OUT / "recordings.json").write_text(json.dumps(evidence, ensure_ascii=False, indent=2), encoding="utf-8")
        print("PASS actual U11L13 token-mention PCM/UTF-16 alignment and worker/cache reopen in three Spanish voices; no player-pause or human-listening claim")


if __name__ == "__main__":
    unittest.main()
