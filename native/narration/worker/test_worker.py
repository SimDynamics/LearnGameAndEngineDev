"""Real Windows Kokoro regression tests; no Android device or curricular edits."""
from __future__ import annotations

import hashlib
import json
from pathlib import Path
import queue
import struct
import subprocess
import threading
import time
import unittest
import wave

ROOT = Path(__file__).resolve().parents[3]
RUNTIME = ROOT / "out/narration/runtime.json"
CACHE = ROOT / "out/qa/narration-worker-tests"


class Worker:
    def __init__(self, missing_resources=False):
        self.config = json.loads(RUNTIME.read_text(encoding="utf-8"))
        arguments = list(self.config["arguments"])
        if missing_resources:
            for key in ("--resources", "--english-resources"):
                arguments[arguments.index(key) + 1] = str(CACHE / "not-installed")
            index = arguments.index("--original-wavs")
            del arguments[index:index + 2]
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


if __name__ == "__main__":
    unittest.main(verbosity=2)

