"""Offline resource preparation checks; no Kokoro model download or synthesis."""
from __future__ import annotations

from contextlib import redirect_stderr, redirect_stdout
import hashlib
import io
from pathlib import Path
import tempfile
import unittest
from unittest.mock import patch

import prepare_narration_ci_resources as prepare


class Response(io.BytesIO):
    def geturl(self) -> str:
        return "https://example.invalid/authenticated-fixture"


def artifact(name: str, payload: bytes) -> prepare.Artifact:
    return prepare.Artifact(name, len(payload), hashlib.sha256(payload).hexdigest(),
                            "https://example.invalid/authenticated-fixture")


class NarrationCiResourcesTest(unittest.TestCase):
    def setUp(self) -> None:
        self.temporary = tempfile.TemporaryDirectory()
        self.addCleanup(self.temporary.cleanup)
        self.root = Path(self.temporary.name)
        self.payload = b"small verification fixture\x00\x01\x02"
        self.artifact = artifact("fixture.onnx", self.payload)
        self.destination = self.root / "models" / self.artifact.name
        # Every network-using test supplies an explicit in-memory response.
        self.network = patch("socket.socket", side_effect=AssertionError("unexpected network access"))
        self.network.start()
        self.addCleanup(self.network.stop)

    @staticmethod
    def snapshot(directory: Path) -> dict[str, tuple[int, str]]:
        return {path.relative_to(directory).as_posix():
                (path.stat().st_mtime_ns, prepare.english_pack.digest(path))
                for path in directory.rglob("*") if path.is_file()}

    def test_current_kotlin_contract_selects_only_the_six_original_spanish_dora_files(self) -> None:
        selected = prepare.spanish_contract()
        self.assertEqual(6, len(selected))
        self.assertEqual(329_952_359, sum(item.size for item in selected))
        self.assertEqual(prepare.SHARED_NAMES | {"ef_dora.bin", "es_lexicon_cache.json"},
                         {item.name for item in selected})
        by_name = {item.name: item for item in selected}
        base = "https://huggingface.co/onnx-community/Kokoro-82M-v1.0-ONNX/resolve/1939ad2a8e416c0acfeecc08a694d14ef25f2231/"
        self.assertEqual(base + "onnx/model.onnx", by_name["model.onnx"].url)
        self.assertEqual(base + "voices/ef_dora.bin", by_name["ef_dora.bin"].url)
        self.assertEqual("f66ec66bd295acb18372e37008533a9a3228483ccd294e7538d5d9294ac9a532",
                         by_name["ef_dora.bin"].sha256)
        self.assertIn("/resolve/d93c5f3eed11847fb81bd6d6a76665e133d7ca77/", by_name["es_lexicon_cache.json"].url)

    def test_contract_rejects_unpinned_urls_duplicate_artifacts_and_wrong_voice_language(self) -> None:
        model = (prepare.NARRATION / "KokoroModelManifest.kt").read_text(encoding="utf-8")
        voice = (prepare.NARRATION / "KokoroVoiceCatalog.kt").read_text(encoding="utf-8")
        model_path, voice_path = self.root / "model.kt", self.root / "voice.kt"
        cases = [
            (model.replace('const val revision = "1939ad2a8e416c0acfeecc08a694d14ef25f2231"',
                           'const val revision = "main"'), voice),
            (model.replace("https://huggingface.co", "http://huggingface.co"), voice),
            (model.replace('Artifact("tokenizer.json",', 'Artifact("config.json",'), voice),
            (model, voice.replace('KokoroVoice("ef_dora", NarrationLanguage.SPANISH',
                                  'KokoroVoice("ef_dora", NarrationLanguage.FRENCH')),
        ]
        for changed_model, changed_voice in cases:
            with self.subTest(model=changed_model[:40], voice=changed_voice[-100:]):
                model_path.write_text(changed_model, encoding="utf-8")
                voice_path.write_text(changed_voice, encoding="utf-8")
                with self.assertRaises(ValueError):
                    prepare.spanish_contract(model_path, voice_path)

    def test_verified_download_is_published_once_and_reused_unchanged(self) -> None:
        with patch.object(prepare.urllib.request, "urlopen", return_value=Response(self.payload)) as opener:
            prepare.download_verified(self.destination, self.artifact)
            opener.assert_called_once()
        self.assertEqual(self.payload, self.destination.read_bytes())
        before = self.snapshot(self.destination.parent)
        with patch.object(prepare.urllib.request, "urlopen", side_effect=AssertionError("cache must be offline")):
            prepare.download_verified(self.destination, self.artifact)
        self.assertEqual(before, self.snapshot(self.destination.parent))
        self.assertEqual([self.destination], list(self.destination.parent.iterdir()))

    def test_existing_different_file_is_preserved_before_any_download(self) -> None:
        self.destination.parent.mkdir()
        self.destination.write_bytes(b"X" * len(self.payload))
        before = self.snapshot(self.destination.parent)
        earlier = artifact("first.onnx", b"first")
        with patch.object(prepare.urllib.request, "urlopen", side_effect=AssertionError("network attempted")):
            with self.assertRaisesRegex(ValueError, "SHA-256 mismatch"):
                prepare.prepare_spanish(self.destination.parent, [earlier, self.artifact])
        self.assertEqual(before, self.snapshot(self.destination.parent))

    def test_partial_corrupt_oversized_or_interrupted_download_never_publishes(self) -> None:
        class Interrupted(Response):
            def read(self, size=-1):
                raise OSError("interrupted fixture transfer")
        cases = (Response(self.payload[:-1]), Response(b"X" * len(self.payload)),
                 Response(self.payload + b"extra"), Interrupted(self.payload))
        for response in cases:
            with self.subTest(response=type(response).__name__, size=len(response.getvalue())):
                with patch.object(prepare.urllib.request, "urlopen", return_value=response):
                    with self.assertRaises((OSError, ValueError)):
                        prepare.download_verified(self.destination, self.artifact)
                self.assertFalse(self.destination.exists())
                self.assertEqual([], list(self.destination.parent.iterdir()))

    def test_concurrent_different_destination_is_never_overwritten(self) -> None:
        original = b"another writer owns this file"
        def responding(*args, **kwargs):
            self.destination.write_bytes(original)
            return Response(self.payload)
        with patch.object(prepare.urllib.request, "urlopen", side_effect=responding):
            with self.assertRaises(ValueError):
                prepare.download_verified(self.destination, self.artifact)
        self.assertEqual(original, self.destination.read_bytes())
        self.assertEqual([self.destination], list(self.destination.parent.iterdir()))

    def test_real_english_pack_stages_all_seventeen_files_without_changing_source(self) -> None:
        source = prepare.english_pack.DEFAULT_ASSETS
        before = self.snapshot(source)
        destination = self.root / "english"
        prepare.stage_english(destination)
        result = prepare.english_pack.verify_staged_assets(destination)
        self.assertEqual((8, 12_915_892, 9),
                         (result.artifact_count, result.total_artifact_bytes, result.metadata_file_count))
        self.assertEqual(17, len(self.snapshot(destination)))
        self.assertEqual(before, self.snapshot(source))
        for name in before:
            with self.subTest(name=name):
                raw = (source / name).read_bytes()
                expected = raw.replace(b"\n", b"\r\n") if name in prepare.CRLF_CONFIGURATIONS and b"\r" not in raw else raw
                self.assertEqual(expected, (destination / name).read_bytes())
        staged = self.snapshot(destination)
        prepare.stage_english(destination)
        self.assertEqual(staged, self.snapshot(destination))

    def test_crlf_materialization_requires_the_exact_named_original_hash(self) -> None:
        raw = b'{\n  "fixture": 1\n}\n'
        original = raw.replace(b"\n", b"\r\n")
        for name in prepare.CRLF_CONFIGURATIONS:
            with self.subTest(name=name):
                selected = artifact(name, original)
                self.assertEqual(original, prepare._english_payload(raw, selected))
                self.assertEqual(original, prepare._english_payload(original, selected))
                for invalid in (raw.replace(b"1", b"2"), raw.replace(b"\n", b"\r", 1)):
                    with self.assertRaises(ValueError):
                        prepare._english_payload(invalid, selected)
        with self.assertRaises(ValueError):
            prepare._english_payload(raw, artifact("spacy_pos.json", original))

    def test_english_staging_preserves_an_incompatible_existing_notice(self) -> None:
        destination = self.root / "english"
        (destination / "licenses").mkdir(parents=True)
        (destination / "licenses/NOTICE.txt").write_bytes(b"do not replace another notice")
        before = self.snapshot(destination)
        with self.assertRaises(ValueError):
            prepare.stage_english(destination)
        self.assertEqual(before, self.snapshot(destination))

    def test_verify_only_checks_both_destinations_without_writes_or_network(self) -> None:
        resources, english = self.root / "models", self.root / "english"
        resources.mkdir()
        selected = [artifact(f"fixture-{index}.onnx", bytes([index]) * 8) for index in range(6)]
        for index, item in enumerate(selected):
            (resources / item.name).write_bytes(bytes([index]) * 8)
        prepare.stage_english(english)
        before = self.snapshot(self.root)
        arguments = ["--resources", str(resources), "--english-resources", str(english), "--verify-only"]
        with patch.object(prepare, "spanish_contract", return_value=selected), redirect_stdout(io.StringIO()):
            self.assertEqual(0, prepare.main(arguments))
        self.assertEqual(before, self.snapshot(self.root))
        (resources / selected[0].name).write_bytes(b"tampered")
        before = self.snapshot(self.root)
        with patch.object(prepare, "spanish_contract", return_value=selected), redirect_stderr(io.StringIO()):
            self.assertEqual(1, prepare.main(arguments))
        self.assertEqual(before, self.snapshot(self.root))

    def test_source_nested_and_linked_destinations_are_rejected(self) -> None:
        for resources, english in ((prepare.ROOT, self.root / "english"),
                                   (prepare.ROOT / "app/new", self.root / "english"),
                                   (self.root / "shared", self.root / "shared/english")):
            with self.subTest(resources=resources, english=english):
                with self.assertRaises(ValueError):
                    prepare.destinations(resources, english)
        external = self.root / "external"
        external.mkdir()
        linked = self.root / "linked"
        try:
            linked.symlink_to(external, target_is_directory=True)
        except OSError as error:
            self.skipTest(f"Creating symlinks is unavailable: {error}")
        with self.assertRaisesRegex(ValueError, "Linked"):
            prepare.destinations(linked / "models", self.root / "english")
        self.assertEqual([], list(external.iterdir()))


if __name__ == "__main__":
    unittest.main(verbosity=2)
