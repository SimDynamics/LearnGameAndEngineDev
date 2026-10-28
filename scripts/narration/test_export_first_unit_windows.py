"""Manifest/coverage regressions without models, synthesis or learner data."""
import copy
import io
import json
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest
from unittest.mock import patch
import wave

from export_first_unit_windows import (LESSONS, missing_recordings, read_manifest,
                                       validate_manifest, write_manifest)


def fixture():
    lessons = sorted(LESSONS)
    return {"format": 1, "course": "cpp_fundamentals", "unit": "intro_programming",
        "lessons": lessons, "uniqueSentences": 3, "entries": [
            {"hash": str(index) * 64, "language": "es", "voice": "ef_dora", "lesson": lesson,
             "location": f"programming/cpp_fundamentals/cpp_fundamentals__intro_programming/{lesson}/1"}
            for index, lesson in enumerate(lessons, 1)]}


def pcm():
    data = io.BytesIO()
    with wave.open(data, "wb") as audio:
        audio.setnchannels(1)
        audio.setsampwidth(2)
        audio.setframerate(24000)
        audio.writeframes(b"\x01\x00" * 120)
    return data.getvalue()


class WindowsFirstUnitExportTest(unittest.TestCase):
    def test_contract_rejects_voice_language_catalogue_and_identity_drift(self):
        valid = fixture()
        self.assertEqual(3, len(validate_manifest(valid)))
        mutations = [lambda m: m["entries"][0].update(voice="af_heart"),
            lambda m: m["entries"][0].update(language="en-US"),
            lambda m: m["lessons"].pop(),
            lambda m: m.update(unit="programas_y_ejecucion"),
            lambda m: m["entries"][0].update(hash="../outside"),
            lambda m: m["entries"][0].update(location="another lesson"),
            lambda m: m["entries"][0].update(location=m["entries"][0]["location"][:-1] + "0"),
            lambda m: m["entries"][0].update(location=m["entries"][0]["location"][:-1] + "../other"),
            lambda m: m["entries"][1].update(hash=m["entries"][0]["hash"])]
        for mutate in mutations:
            candidate = copy.deepcopy(valid)
            mutate(candidate)
            with self.assertRaises(ValueError):
                validate_manifest(candidate)

    def test_coverage_reports_missing_and_corrupt_without_touching_existing_audio(self):
        with tempfile.TemporaryDirectory() as directory:
            folder = Path(directory)
            manifest = fixture()
            keys = [entry["hash"] for entry in manifest["entries"]]
            wav = folder / f"{keys[0]}.wav"
            wav.write_bytes(pcm())
            broken = folder / f"{keys[1]}.wav"
            broken.write_bytes(b"truncated")
            before = {path.name: path.read_bytes() for path in folder.iterdir()}
            self.assertEqual(keys[1:], missing_recordings(manifest, [folder]))
            self.assertEqual(before, {path.name: path.read_bytes() for path in folder.iterdir()})

    def test_manifest_write_validates_before_replacing_existing_export(self):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "manifest.json"
            write_manifest(path, fixture())
            before = path.read_bytes()
            invalid = fixture()
            invalid["entries"][0]["voice"] = "af_heart"
            with self.assertRaises(ValueError):
                write_manifest(path, invalid)
            self.assertEqual(before, path.read_bytes())
            self.assertEqual([path], list(path.parent.iterdir()))

    def test_truncated_opus_header_does_not_pass_coverage(self):
        with tempfile.TemporaryDirectory() as directory:
            folder = Path(directory)
            manifest = fixture()
            key = manifest["entries"][0]["hash"]
            (folder / f"{key}.ogg").write_bytes(b"OggS" + b"\0" * 24 + b"OpusHead")
            with patch("export_first_unit_windows.subprocess.run",
                    return_value=subprocess.CompletedProcess([], 1, b"", b"invalid Ogg")):
                self.assertEqual(3, len(missing_recordings(manifest, [folder], ffmpeg="ffmpeg")))

    def test_worker_request_does_not_prepare_or_choose_preferences(self):
        with tempfile.TemporaryDirectory() as directory:
            runtime = Path(directory) / "runtime.json"
            # This real asynchronous fixture cancels if shutdown/EOF arrives
            # while export is queued, exactly like the Kotlin worker.
            worker = Path(directory) / "worker.py"
            worker.write_text('''import json, queue, sys, threading, time
request = json.loads(sys.stdin.readline())
assert request == {"id":"manifest", "command":"first_unit_manifest"}
following = queue.Queue()
threading.Thread(target=lambda: following.put(sys.stdin.readline()), daemon=True).start()
time.sleep(0.1)
reply = {"id":"manifest", "command":"first_unit_manifest"}
if not following.empty():
    reply["error"] = "cancelled by premature shutdown or EOF"
else:
    reply["manifest"] = json.load(open(sys.argv[1], encoding="utf-8"))
print(json.dumps(reply), flush=True)
shutdown = json.loads(following.get(timeout=5))
assert shutdown == {"id":"shutdown", "command":"shutdown"}
''', encoding="utf-8")
            expected = Path(directory) / "expected.json"
            expected.write_text(json.dumps(fixture()), encoding="utf-8")
            runtime.write_text(json.dumps({"java": sys.executable,
                "arguments": [str(worker), str(expected)]}), encoding="utf-8")
            self.assertEqual(fixture(), read_manifest(runtime))


if __name__ == "__main__":
    unittest.main()
