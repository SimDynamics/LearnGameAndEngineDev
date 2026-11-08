"""Real Windows Kokoro regression tests; no Android device or curricular edits."""
from __future__ import annotations

import hashlib
import json
import os
from pathlib import Path
import queue
import re
import struct
import subprocess
import tempfile
import threading
import time
import unittest
import wave
import xml.etree.ElementTree as ET

ROOT = Path(__file__).resolve().parents[3]
RUNTIME = ROOT / "out/narration/runtime.json"
CACHE = ROOT / "out/qa/narration-worker-tests"


class Worker:
    def __init__(self, missing_resources=False, assets_directory=None):
        self.config = json.loads(RUNTIME.read_text(encoding="utf-8"))
        arguments = list(self.config["arguments"])
        if missing_resources:
            for key in ("--resources", "--english-resources"):
                arguments[arguments.index(key) + 1] = str(CACHE / "not-installed")
            if "--original-wavs" in arguments:
                index = arguments.index("--original-wavs")
                del arguments[index:index + 2]
        if assets_directory is not None:
            arguments[arguments.index("--assets") + 1] = str(assets_directory)
        self.process = subprocess.Popen([self.config["java"], *arguments], stdin=subprocess.PIPE,
            stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True, encoding="utf-8",
            creationflags=subprocess.CREATE_NO_WINDOW)
        self.responses = queue.Queue()
        self.errors = []
        self.sequence = 0
        def stdout():
            try:
                for line in self.process.stdout:
                    try:
                        self.responses.put(json.loads(line))
                    except ValueError:
                        self.responses.put({"protocol_error": line})
            finally:
                self.responses.put({"eof": True})
        def stderr():
            for line in self.process.stderr:
                self.errors.append(line.rstrip())
        self.threads = [threading.Thread(target=stdout, daemon=True), threading.Thread(target=stderr, daemon=True)]
        for thread in self.threads:
            thread.start()

    def send(self, command, **fields):
        self.sequence += 1
        identity = str(fields.pop("id", f"request-{self.sequence}"))
        self.process.stdin.write(json.dumps({"id": identity, "command": command, **fields}, ensure_ascii=False) + "\n")
        self.process.stdin.flush()
        return identity

    def receive(self, timeout=90):
        result = self.responses.get(timeout=timeout)
        if "eof" in result or "protocol_error" in result:
            raise AssertionError(f"Worker exited or violated JSONL: {result}; stderr={self.errors}")
        return result

    def ask(self, command, **fields):
        identity = self.send(command, **fields)
        result = self.receive()
        if result.get("id") != identity or result.get("command") != command:
            raise AssertionError(f"Unexpected response identity: {result}")
        if "error" in result:
            raise AssertionError(f"Narration failed: {result}; stderr={self.errors}")
        return result

    def close(self):
        if self.process.poll() is None:
            self.ask("shutdown")
            self.process.stdin.close()
            self.process.wait(timeout=30)
        for thread in self.threads:
            thread.join(timeout=2)
        self.process.stdout.close()
        self.process.stderr.close()
        if self.process.returncode:
            raise AssertionError(f"Worker exit {self.process.returncode}: {self.errors}")


class WindowsNarrationWorkerTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        if not RUNTIME.is_file():
            raise AssertionError("Build the real Windows worker with scripts/windows/Build-NarrationWorker.ps1 first")
        cls.worker = Worker()

    @classmethod
    def tearDownClass(cls):
        cls.worker.close()

    def prepare(self, source, language, voice, directory=None):
        plan = self.worker.ask("plan", text=source, language=language, voice=voice)
        self.assertEqual(1, len(plan["segments"]))
        segment = plan["segments"][0]
        result = self.worker.ask("prepare", text=segment["text"], preparedText=segment["preparedText"],
            language=language, voice=voice, threads=2, outputDirectory=str(directory or CACHE / "audio"))
        self.assertEqual(segment["key"], result["key"])
        self.assertEqual(source, result["sourceText"])
        self.assertEqual(24000, result["sampleRate"])
        self.assertGreater(result["durationMs"], 200)
        with wave.open(result["wav"], "rb") as audio:
            self.assertEqual((1, 24000, 2), (audio.getnchannels(), audio.getframerate(), audio.getsampwidth()))
            self.assertEqual(result["sampleCount"], audio.getnframes())
            samples = struct.unpack("<" + "h" * audio.getnframes(), audio.readframes(audio.getnframes()))
            self.assertGreater(max(map(abs, samples)), 50)
        return segment, result

    def test_01_plan_uses_original_source_offsets_and_exact_stable_identity(self):
        source = "¿Qué muestra C++? La línea #include conserva std::cout."
        plan = self.worker.ask("plan", text=source, language="es", voice="ef_dora")
        self.assertEqual(2, len(plan["segments"]))
        cursor = 0
        for segment in plan["segments"]:
            self.assertGreaterEqual(segment["sourceStart"], cursor)
            self.assertEqual(source[segment["sourceStart"]:segment["sourceEnd"]], segment["text"])
            self.assertRegex(segment["key"], r"^[0-9a-f]{64}$")
            self.assertEqual(len(segment["spokenRanges"]),len(segment["displayedRanges"]))
            for spoken,displayed in zip(segment["spokenRanges"],segment["displayedRanges"]):
                self.assertTrue(0 <= spoken["start"] < spoken["end"] <= len(segment["spokenText"]))
                self.assertTrue(0 <= displayed["start"] < displayed["end"] <= len(segment["text"]))
            cursor = segment["sourceEnd"]
        identity = dict(page="fixture-page", language="es", voice="ef_dora",
                        texts=[item["preparedText"] for item in plan["segments"]])
        first = self.worker.ask("identity", **identity)["key"]
        self.assertEqual(first, self.worker.ask("identity", **identity)["key"])
        self.assertNotEqual(first, self.worker.ask("identity", **{**identity, "page": "another-page"})["key"])
        self.assertNotEqual(first, self.worker.ask("identity", **identity, engine="ANDROID")["key"])

    def test_02_real_kokoro_all_four_languages_unknown_words_and_reuse(self):
        cases = [
            ("es", "ef_dora", "La línea #include y std::cout muestran una salida."),
            ("fr", "ff_siwis", "La directive #include et std::cout affichent un zorbulateur."),
            ("en-US", "af_heart", "The #include directive and std::cout display a zorbulator."),
            ("en-GB", "bf_emma", "The #include directive and std::cout display a zorbulator."),
        ]
        keys = set()
        for language, voice, source in cases:
            with self.subTest(language=language, voice=voice):
                segment, result = self.prepare(source, language, voice)
                self.assertTrue(result["alignmentAvailable"], result)
                self.assertGreater(len(result["timings"]), 4)
                self.assertEqual(0, min(row["start"] for row in result["timings"]))
                self.assertEqual(len(source), max(row["end"] for row in result["timings"]))
                starts = [row["startMs"] for row in result["timings"]]
                self.assertEqual(sorted(starts), starts)
                self.assertGreater(len(set(round(b - a, 4) for a, b in zip(starts, starts[1:]))), 2,
                    "Word timing must follow the actual model durations, not uniform intervals")
                for row in result["timings"]:
                    self.assertTrue(0 <= row["start"] < row["end"] <= len(source))
                    self.assertTrue(0 <= row["startMs"] < row["endMs"] <= result["durationMs"])
                path = Path(result["wav"])
                before = (path.stat().st_mtime_ns, hashlib.sha256(path.read_bytes()).hexdigest())
                repeated = self.worker.ask("prepare", text=segment["text"], preparedText=segment["preparedText"],
                    language=language, voice=voice, threads=2, outputDirectory=str(CACHE / "audio"))
                self.assertTrue(repeated["cacheHit"])
                self.assertEqual(before, (path.stat().st_mtime_ns, hashlib.sha256(path.read_bytes()).hexdigest()))
                self.assertEqual(result["timings"], repeated["timings"])
                keys.add(result["key"])
        self.assertEqual(4, len(keys))

    def test_03_spanish_math_is_progressive_through_numbers_and_operators(self):
        source = "En nuestro sistema, 6 × 100 = 600 representa seis centenas."
        _, result = self.prepare(source, "es", "ef_dora")
        print("Spanish PCM evidence: " + json.dumps({name: result[name] for name in
            ("audioOrigin", "cacheHit", "sampleCount", "sampleRate", "durationMs")}, sort_keys=True), flush=True)
        if os.environ.get("LGAED_REQUIRE_NEW_SPANISH_SYNTHESIS") == "1":
            self.assertIs(result["cacheHit"], False, result)
            self.assertEqual("synthesized", result["audioOrigin"], result)
        self.assertTrue(result["alignmentAvailable"], result)
        for operator in ("×", "="):
            position = source.index(operator)
            self.assertTrue(any(row["start"] <= position < row["end"] for row in result["timings"]))
        self.assertLess(max(row["endMs"] for row in result["timings"] if row["end"] <= source.index("representa")),
                        result["durationMs"])

    def test_04_real_historical_dora_audio_is_reused_without_resynthesis(self):
        source = "¿Qué es la programación?"
        segment, result = self.prepare(source, "es", "ef_dora", CACHE / "historical")
        self.assertTrue((ROOT / "app/src/main/assets/narration/prerendered" / (segment["key"] + ".ogg")).is_file())
        self.assertTrue(result["cacheHit"])
        self.assertIn(result["audioOrigin"], ("existing-recording", "packaged-recording", "cache"))

    def test_05_packaged_recording_remains_playable_without_installed_models(self):
        offline = Worker(missing_resources=True)
        try:
            segment = offline.ask("plan", text="¿Qué es la programación?", language="es")["segments"][0]
            result = offline.ask("prepare", text=segment["text"], preparedText=segment["preparedText"], language="es",
                voice="ef_dora", outputDirectory=str(CACHE / "offline-pack"), threads=2)
            self.assertTrue(result["cacheHit"])
            self.assertIn(result["audioOrigin"], ("packaged-recording", "cache"))
            self.assertGreater(result["sampleCount"], 24000)
            self.assertFalse(result["alignmentAvailable"])
        finally:
            offline.close()

    def test_06_cancel_is_received_while_an_actual_prepare_job_is_pending(self):
        source = ("La biblioteca reúne recursos ya preparados para todas las operaciones y los cálculos, " * 9).rstrip(", ") + "."
        segment = self.worker.ask("plan", text=source, language="es")["segments"][0]
        identity = self.worker.send("prepare", text=segment["text"], preparedText=segment["preparedText"],
            language="es", voice="ef_dora", threads=2, outputDirectory=str(CACHE / "cancelled"))
        time.sleep(.1)
        self.worker.send("cancel", id=identity)
        responses = [self.worker.receive(), self.worker.receive()]
        self.assertEqual({"cancel", "prepare"}, {item["command"] for item in responses})
        failure = next(item for item in responses if item["command"] == "prepare")
        self.assertTrue(failure.get("cancelled"), failure)
        self.assertIn("error", failure)
        self.assertFalse((CACHE / "cancelled" / (segment["key"] + ".wav")).exists())
        self.assertEqual([], list((CACHE / "cancelled").glob("*.part")))

    def test_audio_budget_stops_only_new_audio_and_preserves_cached_recordings(self):
        segment, recorded = self.prepare("¿Qué es la programación?", "es", "ef_dora", CACHE / "budget")
        path = Path(recorded["wav"])
        original = (path.stat().st_mtime_ns, hashlib.sha256(path.read_bytes()).hexdigest())
        reused = self.worker.ask("prepare", text=segment["text"], preparedText=segment["preparedText"],
            language="es", voice="ef_dora", threads=2, outputDirectory=str(CACHE / "budget"), audioBudgetBytes=0)
        self.assertTrue(reused["cacheHit"])
        self.assertEqual(original, (path.stat().st_mtime_ns, hashlib.sha256(path.read_bytes()).hexdigest()))
        fresh = self.worker.ask("plan", text="La reserva privada no borra grabaciones ya verificadas.", language="es")["segments"][0]
        self.worker.send("prepare", text=fresh["text"], preparedText=fresh["preparedText"], language="es",
            voice="ef_dora", threads=2, outputDirectory=str(CACHE / "budget"), audioBudgetBytes=0)
        rejected = self.worker.receive()
        self.assertIn("storage limit reached", rejected.get("error", ""))
        self.assertFalse((CACHE / "budget" / (fresh["key"] + ".wav")).exists())
        self.assertEqual(original, (path.stat().st_mtime_ns, hashlib.sha256(path.read_bytes()).hexdigest()))
        self.assertIn("segments", self.worker.ask("plan", text="La lectura continúa.", language="es"))

    def test_07_verdict_icons_reuse_spoken_audio_and_follow_first_word(self):
        cases = [
            ("es", "ef_dora", "Correcto", "Incorrecto", "La expresión 6 × 100 = 600 conserva su valor."),
            ("fr", "ff_siwis", "Correct", "Incorrect", "La ligne #include conserve std::cout."),
            ("en-US", "af_heart", "Correct", "Incorrect", "The #include directive preserves std::cout."),
            ("en-GB", "bf_emma", "Correct", "Incorrect", "The #include directive preserves std::cout."),
        ]
        for language, voice, correct, incorrect, explanation in cases:
            for icon, verdict in (("✓", correct), ("✗", incorrect)):
                with self.subTest(language=language, icon=icon):
                    plain = f"{verdict}.\n\n{explanation}"
                    visible = f"{icon} {plain}"
                    baseline = self.worker.ask("plan", text=plain, language=language, voice=voice)["segments"]
                    decorated = self.worker.ask("plan", text=visible, language=language, voice=voice)["segments"]
                    self.assertEqual(len(baseline), len(decorated))
                    for before, after in zip(baseline, decorated):
                        # Only presentation differs: existing recordings remain
                        # reusable and the actual frontend phones are unchanged.
                        for field in ("preparedText", "spokenText", "spokenRanges", "key"):
                            self.assertEqual(before[field], after[field], field)
                        self.assertNotIn(icon, after["spokenText"])
                        self.assertEqual(visible[after["sourceStart"]:after["sourceEnd"]], after["text"])
                        self.assertTrue(after["displayedRanges"], "The verdict must retain word alignment")
                    first = decorated[0]
                    self.assertEqual(0, first["sourceStart"])
                    self.assertTrue(first["text"].startswith(f"{icon} {verdict}"))
                    self.assertEqual(0, first["displayedRanges"][0]["start"])
                    self.assertGreaterEqual(first["displayedRanges"][0]["end"], len(verdict) + 2)
                    # The rule is precisely Android's leading-icon rule; marks
                    # embedded in source are not silently discarded.
                    embedded = self.worker.ask("plan", text=f"{verdict} {icon} {explanation}",
                                               language=language, voice=voice)["segments"]
                    self.assertTrue(any(icon in part["preparedText"] for part in embedded))

    def test_08_named_letter_keeps_escape_and_original_source_ranges(self):
        cases = [
            (r"La secuencia \n incluye la barra inversa y la letra n.", "la letra ene"),
            ("No se ven la barra inversa ni la letra n: juntas forman un salto.", "la letra ene:"),
            ("La LETRA N: se nombra.", "LETRA ene:"),
            ("El carácter n se lee como una letra.", "carácter ene"),
        ]
        for source, expected in cases:
            with self.subTest(source=source):
                segments = self.worker.ask("plan", text=source, language="es")["segments"]
                spoken = " ".join(part["spokenText"] for part in segments)
                self.assertIn(expected, spoken)
                if r"\n" in source:
                    self.assertIn("barra inversa ene", spoken)
                for part in segments:
                    self.assertEqual(source[part["sourceStart"]:part["sourceEnd"]], part["text"])
                    self.assertTrue(part["displayedRanges"], "Named letters must retain original word alignment")
                    self.assertEqual(len(part["spokenRanges"]), len(part["displayedRanges"]))
                    for shown in part["displayedRanges"]:
                        self.assertGreater(shown["end"], shown["start"])
                        self.assertLessEqual(shown["end"], len(part["text"]))
        ordinary = "La letra número continúa y el carácter nñ conserva su escritura."
        spoken = " ".join(part["spokenText"] for part in self.worker.ask(
            "plan", text=ordinary, language="es", mathMode="text")["segments"])
        self.assertEqual(ordinary, spoken)
        named = self.worker.ask("plan", text="La letra n.", language="es")["segments"]
        spelled = self.worker.ask("plan", text="La letra ene.", language="es")["segments"]
        self.assertEqual([part["key"] for part in named], [part["key"] for part in spelled])

    def test_09_prose_pauses_share_foreground_and_background_preparation(self):
        source = "Resultado: continúa (con cuidado) y termina; después comprueba."
        segments = self.worker.ask("plan", text=source, language="es")["segments"]
        self.assertEqual(["Resultado:", "continúa (", "con cuidado) y termina;", "después comprueba."],
                         [part["spokenText"] for part in segments])
        sources = [source, "Resultado: (con cuidado) y termina.",
                   "No se ven la barra inversa ni la letra n: juntas forman un salto.",
                   r"La secuencia \n conserva su nombre.",
                   "main() utiliza std::cout y el carácter ':' junto con '('.",
                   r"La fórmula \(3 + (2 * 4)\) mantiene su agrupación.",
                   "Consulta https://ejemplo.test/ruta: continúa."]
        for source in sources:
            with self.subTest(source=source):
                foreground = self.worker.ask("plan", text=source, language="es", voice="ef_dora")["segments"]
                lesson = {"pages": [{"blocks": [{"type": "paragraph", "text": source}]}]}
                background = self.worker.ask("curriculum_plan", lessonJson=json.dumps(lesson),
                    page=0, language="es", uiLanguage="es", voice="ef_dora")["segments"]
                for field in ("preparedText", "key"):
                    self.assertEqual([part[field] for part in foreground], [part[field] for part in background], field)
                self.assertTrue(all(part["displayedRanges"] for part in foreground))
                self.assertTrue(all(part["spokenRanges"] for part in foreground), "No punctuation-only audio segments")
        for source in sources[-3:]:
            self.assertEqual(1, len(self.worker.ask("plan", text=source, language="es")["segments"]))

    def test_10_isolated_escaped_quotes_and_backslashes_are_spoken_as_symbols(self):
        samples = [
            (r'Los símbolos \" y \\ representan escapes técnicos.',
             ["barra inversa comilla", "barra inversa barra inversa"]),
            (r'Para escribir una comilla se utiliza \".', ["barra inversa comilla"]),
            (r'La secuencia \\ representa una barra inversa.', ["barra inversa barra inversa"]),
            (r'En std::cout << "\""; se imprime una comilla.', ["barra inversa comilla"]),
        ]
        for source, expected in samples:
            with self.subTest(source=source):
                segments = self.worker.ask("plan", text=source, language="es", voice="ef_dora")["segments"]
                spoken = " ".join(segment["spokenText"] for segment in segments)
                for phrase in expected:
                    self.assertIn(phrase, spoken)
                self.assertNotIn("barra inversa comilla doble", spoken)
                for part in segments:
                    self.assertEqual(source[part["sourceStart"]:part["sourceEnd"]], part["text"])
                    self.assertEqual(len(part["spokenRanges"]), len(part["displayedRanges"]))
                    self.assertTrue(part["displayedRanges"])
                lesson = {"pages": [{"blocks": [{"type": "paragraph", "text": source}]}]}
                background = self.worker.ask("curriculum_plan", lessonJson=json.dumps(lesson),
                    page=0, language="es", uiLanguage="es", voice="ef_dora")["segments"]
                self.assertEqual([part["preparedText"] for part in segments],
                                 [part["preparedText"] for part in background])
                self.assertEqual([part["key"] for part in segments],
                                 [part["key"] for part in background])
        path = r'La ruta C:\\carpeta no es una secuencia aislada.'
        spoken = " ".join(p["spokenText"] for p in self.worker.ask("plan", text=path, language="es")["segments"])
        self.assertNotIn("barra inversa barra inversa", spoken)


class HistoricalDoraPackTest(unittest.TestCase):
    """Explicit historical export never needs models, catalog entries or student state."""

    LESSON_NAMES = ("what_is_programming", "first_contact_cpp", "program_structure")
    LESSON_IDS = tuple("cpp_fundamentals__intro_programming__" + name for name in LESSON_NAMES)

    @staticmethod
    def recording_snapshot():
        return {path.name: (path.stat().st_mtime_ns, hashlib.sha256(path.read_bytes()).hexdigest())
                for path in (ROOT / "app/src/main/assets/narration/prerendered").glob("*.ogg")}

    @staticmethod
    def plain_dora_key(text):
        # This fixture has no changed technical reading or manual English term.
        # Its identity must remain the original KokoroNarrationCache payload.
        source = (ROOT / "app/src/main/java/com/learngraphicsprogramming/app/narration/KokoroModelManifest.kt")
        revision = re.search(r'const val revision = "([0-9a-f]+)"', source.read_text(encoding="utf-8"))[1]
        return hashlib.sha256("\0".join(("kokoro-wav-v1", revision, "es", "ef_dora", text)).encode()).hexdigest()

    def write_fixture(self, assets):
        folder = assets / "content/es/programming/cpp_fundamentals/intro_programming"
        folder.mkdir(parents=True)
        titles = ("Primera lección.", "Segunda lección.", "Tercera lección.")
        for name, identity, title in zip(self.LESSON_NAMES, self.LESSON_IDS, titles):
            pages = [{"blocks": [{"type": "paragraph", "text": "Una frase común."}]}]
            if name == "what_is_programming":
                pages[0]["blocks"].insert(0, {"type": "paragraph",
                    "text": "Esta frase conserva su pausa: la oración sigue aquí."})
                pages.append({"blocks": [
                    {"type": "paragraph", "text": "Una frase común."},
                    {"type": "order_code", "question": "Ordena los bloques.",
                     "fragments": ["NO_NARRAR_CODIGO"], "correctOrder": [0],
                     "explanation": "NO_NARRAR_SOLUCION"},
                    {"type": "drag_fill_code", "question": "Completa los espacios.",
                     "template": "NO_NARRAR_PLANTILLA", "reviewFeedback": "NO_NARRAR_ERROR"}]})
            (folder / f"lesson_{name}.json").write_text(json.dumps(
                {"id": identity, "title": title, "pages": pages}, ensure_ascii=False), encoding="utf-8")
        # A broken active catalog must not select or prevent this fixed historical export.
        (assets / "content/es/programming/index.json").write_text("not a catalog", encoding="utf-8")
        return folder

    def test_manifest_is_fixed_spanish_dora_and_preserves_every_existing_recording(self):
        before = self.recording_snapshot()
        worker = Worker(missing_resources=True)
        try:
            manifest = worker.ask("first_unit_manifest")["manifest"]
            conflicting_preferences = worker.ask("first_unit_manifest", language="fr", voice="ff_siwis",
                uiLanguage="en-US", page=999, lessonJson="not a lesson")["manifest"]
            self.assertEqual(manifest, conflicting_preferences)
            self.assertEqual((1, "cpp_fundamentals", "intro_programming"),
                (manifest["format"], manifest["course"], manifest["unit"]))
            self.assertEqual(list(self.LESSON_IDS), manifest["lessons"])
            entries = manifest["entries"]
            self.assertGreater(len(entries), 0)
            self.assertEqual(manifest["uniqueSentences"], len(entries))
            self.assertEqual(len(entries), len({entry["hash"] for entry in entries}))
            for entry in entries:
                self.assertEqual({"hash", "language", "voice", "lesson", "location"}, set(entry))
                self.assertEqual(("es", "ef_dora"), (entry["language"], entry["voice"]))
                self.assertRegex(entry["hash"], r"^[0-9a-f]{64}$")
                self.assertIn(entry["lesson"], self.LESSON_IDS)
                page = int(entry["location"].rsplit("/", 1)[1])
                name = entry["lesson"].rsplit("__", 1)[1]
                lesson = json.loads((ROOT / "app/src/main/assets/content/es/programming/cpp_fundamentals/"
                    f"intro_programming/lesson_{name}.json").read_text(encoding="utf-8"))
                self.assertTrue(1 <= page <= len(lesson["pages"]))
                self.assertEqual(f"programming/cpp_fundamentals/cpp_fundamentals__intro_programming/"
                    f"{entry['lesson']}/{page}", entry["location"])
            # The actual reader and the historical exporter retain the same known title key.
            title = "¿Qué es la programación?"
            key = worker.ask("plan", text=title, language="es", voice="ef_dora")["segments"][0]["key"]
            self.assertEqual(self.plain_dora_key(title), key)
            self.assertIn(key, {entry["hash"] for entry in entries})
        finally:
            worker.close()
        self.assertEqual(before, self.recording_snapshot())

    def test_shared_extractor_sentence_boundaries_deduplication_and_spanish_instructions(self):
        with tempfile.TemporaryDirectory(prefix="lgaed-dora-export-") as temporary:
            assets = Path(temporary)
            self.write_fixture(assets)
            worker = Worker(missing_resources=True, assets_directory=assets)
            try:
                manifest = worker.ask("first_unit_manifest", language="en-GB", voice="bf_emma")["manifest"]
                instructions = ET.parse(ROOT / "app/src/main/res/values/strings.xml").getroot()
                strings = {element.attrib.get("name"): element.text for element in instructions.findall("string")}
                sentences = ["Primera lección.", "Esta frase conserva su pausa: la oración sigue aquí.",
                    "Una frase común.", "Ordena los bloques.", strings["exercise_order_instruction"],
                    "Ejercicio de código C más más.", "Completa los espacios.",
                    strings["exercise_fill_instruction"], "Segunda lección.", "Tercera lección."]
                self.assertEqual([self.plain_dora_key(text) for text in sentences],
                    [entry["hash"] for entry in manifest["entries"]])
                common = next(entry for entry in manifest["entries"]
                    if entry["hash"] == self.plain_dora_key("Una frase común."))
                self.assertEqual(self.LESSON_IDS[0], common["lesson"])
                self.assertTrue(common["location"].endswith("/1"))
            finally:
                worker.close()

    def test_malformed_or_incomplete_fixed_lesson_fails_instead_of_exporting_partial_pack(self):
        for defect in ("missing", "identity", "empty_pages", "malformed_page"):
            with self.subTest(defect=defect), tempfile.TemporaryDirectory(prefix="lgaed-dora-invalid-") as temporary:
                assets = Path(temporary)
                folder = self.write_fixture(assets)
                path = folder / "lesson_program_structure.json"
                lesson = json.loads(path.read_text(encoding="utf-8"))
                if defect == "missing":
                    path.unlink()
                else:
                    if defect == "identity":
                        lesson["id"] = "another_lesson"
                    elif defect == "empty_pages":
                        lesson["pages"] = []
                    else:
                        lesson["pages"] = ["not a page object"]
                    path.write_text(json.dumps(lesson), encoding="utf-8")
                worker = Worker(missing_resources=True, assets_directory=assets)
                try:
                    worker.send("first_unit_manifest")
                    reply = worker.receive()
                    self.assertIn("error", reply)
                    self.assertNotIn("manifest", reply)
                finally:
                    worker.close()

    def test_manifest_recording_and_compatible_timings_reuse_without_models(self):
        before = self.recording_snapshot()
        with tempfile.TemporaryDirectory(prefix="lgaed-dora-offline-") as temporary:
            worker = Worker(missing_resources=True)
            try:
                title = "¿Qué es la programación?"
                manifest = worker.ask("first_unit_manifest")["manifest"]
                segment = worker.ask("plan", text=title, language="es", voice="ef_dora")["segments"][0]
                self.assertIn(segment["key"], {entry["hash"] for entry in manifest["entries"]})
                fields = dict(text=segment["text"], preparedText=segment["preparedText"], language="es",
                    voice="ef_dora", outputDirectory=temporary)
                audio = worker.ask("prepare", **fields)
                self.assertTrue(audio["cacheHit"])
                self.assertEqual("packaged-recording", audio["audioOrigin"])
                self.assertFalse(audio["alignmentAvailable"])
                wav = Path(audio["wav"])
                audio_hash = hashlib.sha256(wav.read_bytes()).hexdigest()
                # A bounded alignment fixture checks preservation of an existing valid sidecar.
                timings = [{"start": 0, "end": len(title), "startMs": 0.0, "endMs": audio["durationMs"]}]
                sidecar = wav.with_name(f"{segment['key']}-{hashlib.sha256(title.encode()).hexdigest()}.timings.json")
                sidecar.write_text(json.dumps({"audioSha256": audio_hash,
                    "sampleCount": audio["sampleCount"], "timings": timings}), encoding="utf-8")
                snapshot = [(path.stat().st_mtime_ns, path.read_bytes()) for path in (wav, sidecar)]
                repeated = worker.ask("prepare", **fields)
                self.assertEqual("cache", repeated["audioOrigin"])
                self.assertEqual(timings, repeated["timings"])
                self.assertTrue(repeated["alignmentAvailable"])
                self.assertEqual(snapshot, [(path.stat().st_mtime_ns, path.read_bytes()) for path in (wav, sidecar)])
            finally:
                worker.close()
        self.assertEqual(before, self.recording_snapshot())


if __name__ == "__main__":
    unittest.main(verbosity=2)

