"""Windows distribution checks use existing assets and disposable small fixtures."""
import copy
import hashlib
import json
from pathlib import Path
import tempfile
import unittest
from unittest.mock import patch

import verify_windows_distribution as distribution


class WindowsNarrationDistributionTest(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory()
        self.addCleanup(self.temporary.cleanup)
        self.directory = Path(self.temporary.name)
        for name in ("host.exe", "java.exe", "jvm.dll", "worker.jar"):
            (self.directory / name).write_bytes(b"isolated launch-path fixture")
        self.config = {
            "java": str(self.directory / "host.exe"),
            "javaExecutable": str(self.directory / "java.exe"),
            "jvm": str(self.directory / "jvm.dll"),
            "classpath": str(self.directory / "worker.jar"),
            "mainClass": distribution.MAIN_CLASS,
            "assets": str(distribution.ROOT / "app/src/main/assets"),
            "resources": str(self.directory / "absent-models"),
            "englishResources": str(distribution.english_pack.DEFAULT_ASSETS),
            "originalWavs": str(self.directory / "absent-wavs"),
            "ffmpeg": "",
        }
        self.config["arguments"] = [
            self.config["jvm"], self.config["classpath"], self.config["mainClass"],
            "--assets", self.config["assets"], "--resources", self.config["resources"],
            "--english-resources", self.config["englishResources"],
            "--original-wavs", self.config["originalWavs"], "--profile", "Codex"]

    def verify(self, config=None):
        pending = self.directory / "runtime.json.part"
        pending.write_text(json.dumps(self.config if config is None else config), encoding="utf-8")
        return distribution.verify_runtime(pending)

    def test_existing_assets_and_pending_configuration_validate_without_synthesis_models(self):
        watched = [*distribution.english_pack.DEFAULT_ASSETS.rglob("*"),
                   *(distribution.ROOT / "app/src/main/assets/narration/prerendered").glob("*.ogg")]
        before = {path: (path.stat().st_size, path.stat().st_mtime_ns) for path in watched if path.is_file()}
        result = self.verify()
        self.assertEqual((8, 12_915_892, 9), (result.english_artifact_count,
                         result.english_artifact_bytes, result.english_metadata_file_count))
        self.assertEqual(40, result.preserved_prerendered_count)
        self.assertGreaterEqual(result.prerendered_count, 40)
        self.assertGreaterEqual(result.prerendered_bytes, 774_907)
        self.assertFalse(Path(self.config["resources"]).exists())
        self.assertFalse(Path(self.config["originalWavs"]).exists())
        self.assertEqual(before, {path: (path.stat().st_size, path.stat().st_mtime_ns) for path in before})

    def test_rejects_missing_nonabsolute_or_disagreeing_argument_paths(self):
        for field, flag in (("assets", "--assets"), ("resources", "--resources"),
                            ("englishResources", "--english-resources"), ("originalWavs", "--original-wavs")):
            for replacement in (None, "relative/path", str(self.directory / "different")):
                with self.subTest(field=field, replacement=replacement):
                    changed = copy.deepcopy(self.config)
                    changed[field] = replacement
                    with self.assertRaises(ValueError):
                        self.verify(changed)
            with self.subTest(missing_flag=flag):
                changed = copy.deepcopy(self.config)
                index = changed["arguments"].index(flag)
                del changed["arguments"][index:index + 2]
                with self.assertRaises(ValueError):
                    self.verify(changed)

    def test_rejects_duplicate_or_incomplete_arguments(self):
        for additions in (["--assets", self.config["assets"]], ["--profile", "Codex"], ["--assets"]):
            with self.subTest(additions=additions):
                changed = copy.deepcopy(self.config)
                changed["arguments"].extend(additions)
                with self.assertRaises(ValueError):
                    self.verify(changed)

    def test_rejects_foreign_checkout_assets_even_if_fields_agree(self):
        changed = copy.deepcopy(self.config)
        changed["assets"] = str(self.directory / "another-checkout-assets")
        changed["arguments"][changed["arguments"].index("--assets") + 1] = changed["assets"]
        with self.assertRaisesRegex(ValueError, "this checkout"):
            self.verify(changed)

    def test_explicit_english_override_uses_same_offline_contract(self):
        changed = copy.deepcopy(self.config)
        override = self.directory / "explicit-english-pack"
        changed["englishResources"] = str(override)
        changed["arguments"][changed["arguments"].index("--english-resources") + 1] = str(override)
        verified = distribution.english_pack.VerificationResult(8, 12_915_892, 9)
        with patch.object(distribution.english_pack, "verify_staged", return_value=verified) as check_pack, \
                patch.object(distribution, "verify_persistent_attribution") as attribution:
            self.verify(changed)
        self.assertEqual(override, check_pack.call_args.args[0])
        attribution.assert_called_once_with(distribution.ROOT, override)
        with self.assertRaises((ValueError, OSError)):
            self.verify(changed)

    def test_rejects_launch_metadata_or_missing_launch_files(self):
        mutations = [lambda c: c.update(mainClass="wrong.Main"),
                     lambda c: c["arguments"].__setitem__(0, str(self.directory / "another.dll")),
                     lambda c: c["arguments"].__setitem__(1, "another-classpath"),
                     lambda c: c["arguments"].__setitem__(2, "wrong.Main"),
                     lambda c: c["arguments"].__setitem__(-1, "Work"),
                     lambda c: c.update(ffmpeg=str(self.directory / "ffmpeg.exe")),
                     lambda c: c.update(java=str(self.directory / "missing.exe")),
                     lambda c: c.update(classpath=""),
                     lambda c: c.update(classpath=c["classpath"] + distribution.os.pathsep)]
        for mutate in mutations:
            with self.subTest(mutate=mutate):
                changed = copy.deepcopy(self.config)
                mutate(changed)
                with self.assertRaises(ValueError):
                    self.verify(changed)

    def test_spacy_requires_original_attribution_and_full_persistent_lookup_mit(self):
        relative = Path("app/src/main/res/raw/spacy_pos_attribution.txt")
        target = self.directory / relative
        target.parent.mkdir(parents=True)
        original = (distribution.ROOT / relative).read_bytes()
        lookup = (distribution.english_pack.DEFAULT_ASSETS / "licenses/SPACY-LOOKUPS-MIT.txt").read_bytes()
        for data in (b"MIT", lookup, original.replace(b"spaCy", b"xxxxx", 1)):
            with self.subTest(data=data[:30]):
                target.write_bytes(data)
                with self.assertRaises(ValueError):
                    distribution.verify_persistent_attribution(self.directory, distribution.english_pack.DEFAULT_ASSETS)
        target.write_bytes(original.replace(b"\r\n", b"\n"))
        distribution.verify_persistent_attribution(self.directory, distribution.english_pack.DEFAULT_ASSETS)

    def audio_fixture(self):
        directory = self.directory / "narration/prerendered"
        directory.mkdir(parents=True)
        name = "0" * 64 + ".ogg"
        data = b"OggS" + b"\0" * 24 + b"OpusHead" + b"\0" * 32
        (directory / name).write_bytes(data)
        expected = {name: (len(data), hashlib.sha256(data).hexdigest())}
        return directory, name, data, expected

    def test_preserved_opus_rejects_missing_truncated_and_same_size_corruption(self):
        directory, name, data, expected = self.audio_fixture()
        with patch.object(distribution, "PRESERVED_OPUS", expected):
            self.assertEqual((1, len(data)), distribution.verify_prerendered(self.directory))
            for changed in (data[:-1], data[:-1] + b"x"):
                (directory / name).write_bytes(changed)
                with self.assertRaisesRegex(ValueError, "size/SHA-256"):
                    distribution.verify_prerendered(self.directory)
            (directory / name).unlink()
            with self.assertRaises(ValueError):
                distribution.verify_prerendered(self.directory)

    def test_additional_audio_requires_opus_container_and_hash_key(self):
        directory, name, data, expected = self.audio_fixture()
        additional = directory / ("1" * 64 + ".ogg")
        with patch.object(distribution, "PRESERVED_OPUS", expected):
            additional.write_bytes(data)
            self.assertEqual((2, 2 * len(data)), distribution.verify_prerendered(self.directory))
            additional.write_bytes(b"not opus")
            with self.assertRaisesRegex(ValueError, "Ogg Opus"):
                distribution.verify_prerendered(self.directory)
            additional.unlink()
            (directory / "not-a-cache-key.ogg").write_bytes(data)
            with self.assertRaisesRegex(ValueError, "cache key"):
                distribution.verify_prerendered(self.directory)


if __name__ == "__main__":
    unittest.main()
