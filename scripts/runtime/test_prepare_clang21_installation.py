"""Offline tests of source integrity and byte-exact APK envelope reconstruction.

Synthetic ZIP fixtures exercise packaging safeguards, not Android signature or
installation behavior. The CLI separately invokes real Android SDK verification.
"""
import copy
import hashlib
import io
import json
import struct
import sys
import tempfile
import unittest
import zipfile
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import prepare_clang21_installation as prepare


def elf_payload(index):
    value = bytearray(b"\x00" * 4096)
    value[:6] = b"\x7fELF\x02\x01"
    value[16:20] = b"\x03\x00\xb7\x00"
    value[32:] = bytes([index + 1]) * (len(value) - 32)
    return bytes(value)


class PrepareClang21InstallationTest(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory()
        self.addCleanup(self.temporary.cleanup)
        self.root = Path(self.temporary.name)
        self.native, self.cache, self.work = (self.root / name for name in ("native", "cache", "work"))
        for directory in (self.native, self.cache, self.work):
            directory.mkdir()
        self.payloads = {name: elf_payload(index) for index, name in enumerate(prepare.LIBRARIES)}
        for name, value in self.payloads.items():
            (self.native / name).write_bytes(value)
        base = self.cache / prepare.ARCHIVES[0].name
        extension = self.cache / prepare.ARCHIVES[1].name
        with zipfile.ZipFile(base, "w", zipfile.ZIP_DEFLATED) as archive:
            for name in prepare.BASE_LIBRARIES:
                archive.writestr("native/" + name, self.payloads[name])
        nested = io.BytesIO()
        with zipfile.ZipFile(nested, "w", zipfile.ZIP_DEFLATED) as archive:
            for name in prepare.LIBRARIES:
                if name not in prepare.BASE_LIBRARIES:
                    archive.writestr("native/" + name, self.payloads[name])
        with zipfile.ZipFile(extension, "w", zipfile.ZIP_STORED) as archive:
            archive.writestr(prepare.NESTED_ENTRY, nested.getvalue())
        self.archives = tuple(prepare.Archive(path.name, path.stat().st_size, prepare.sha256(path))
                              for path in (base, extension))
        self.libraries = prepare.verify_sources(self.native, self.cache, self.work, self.archives)
        self.apk, self.envelope = self.root / "input.apk", self.root / "envelope.bin"

    def create_apk(self, compression=zipfile.ZIP_STORED, omit=None):
        with zipfile.ZipFile(self.apk, "w", compression) as archive:
            archive.writestr("AndroidManifest.xml", b"\x03\x00\x08\x00synthetic manifest")
            archive.comment = b"retained EOCD comment"
            for name in prepare.LIBRARIES:
                if name != omit:
                    archive.writestr("lib/arm64-v8a/" + name, self.payloads[name])
        return self.apk

    def recipe(self):
        return prepare.create_recipe(self.apk, self.envelope, self.native, self.libraries,
                                     "com.example.qa", 42, "a" * 64, self.archives)

    def test_every_library_retains_original_archive_bytes_and_elf_identity(self):
        self.assertEqual(12, len(self.libraries))
        self.assertFalse((self.work / "verified-cpp-runtime.zip").exists())
        for library in self.libraries:
            self.assertEqual(hashlib.sha256(self.payloads[library["name"]]).hexdigest(), library["sha256"])
            self.assertEqual("native/" + library["name"], library["entry"])
            self.assertEqual(None if library["name"] in prepare.BASE_LIBRARIES else prepare.NESTED_ENTRY,
                             library["nestedEntry"])

    def test_rejects_same_length_archive_tampering(self):
        archive = self.cache / self.archives[0].name
        data = bytearray(archive.read_bytes())
        data[10] ^= 1
        archive.write_bytes(data)
        with self.assertRaisesRegex(ValueError, "archive SHA-256"):
            prepare.verify_sources(self.native, self.cache, self.work, self.archives)

    def test_rejects_prepared_library_tampering(self):
        path = self.native / prepare.LIBRARIES[0]
        data = bytearray(path.read_bytes())
        data[-1] ^= 1
        path.write_bytes(data)
        with self.assertRaisesRegex(ValueError, "differs from its official original"):
            prepare.verify_sources(self.native, self.cache, self.work, self.archives)

    def test_exactly_twelve_prepared_files_are_required(self):
        (self.native / "unexpected.so").write_bytes(elf_payload(99))
        with self.assertRaisesRegex(ValueError, "exactly the twelve"):
            prepare.verify_sources(self.native, self.cache, self.work, self.archives)

    def test_wrong_elf_class_and_architecture_are_rejected(self):
        for index in (0, 4, 5, 16, 18):
            value = bytearray(elf_payload(0))
            value[index] ^= 1
            with self.subTest(index=index), self.assertRaisesRegex(ValueError, "ELF64 AArch64"):
                prepare.verify_elf(value, "bad.so")

    def test_apk_contains_uncompressed_twelve_libraries(self):
        self.create_apk()
        with zipfile.ZipFile(self.apk) as archive:
            infos = [info for info in archive.infolist() if info.filename.startswith("lib/")]
            self.assertEqual(12, len(infos))
            self.assertTrue(all(info.compress_type == zipfile.ZIP_STORED for info in infos))
        recipe = self.recipe()
        self.assertEqual(12, sum(segment["kind"] == "library" for segment in recipe["segments"]))
        self.assertLess(recipe["envelope"]["bytes"], recipe["apk"]["bytes"] // 10)
        self.assertEqual(recipe["apk"]["bytes"], recipe["envelope"]["bytes"] + sum(item["bytes"] for item in self.libraries))

    def test_compressed_native_payload_is_rejected(self):
        self.create_apk(zipfile.ZIP_DEFLATED)
        with self.assertRaisesRegex(ValueError, "ZIP_STORED"):
            self.recipe()

    def test_recipe_exposes_public_integrity_only_and_never_host_signing_inputs(self):
        self.create_apk()
        recipe = self.recipe()
        self.assertEqual({"format", "packageName", "versionCode", "certificateSha256", "splitName",
                          "apk", "envelope", "archives", "libraries", "segments"}, set(recipe))
        self.assertEqual({"bytes", "sha256"}, set(recipe["apk"]))
        self.assertEqual({"file", "bytes", "sha256"}, set(recipe["envelope"]))
        for library in recipe["libraries"]:
            self.assertEqual({"name", "bytes", "sha256", "archive", "entry", "nestedEntry"}, set(library))
        serialized = json.dumps(recipe)
        for forbidden in (str(self.root), "keystore", "password", "privateKey", "storeFile"):
            self.assertNotIn(forbidden, serialized)

    def test_missing_library_in_apk_is_rejected(self):
        self.create_apk(omit=prepare.LIBRARIES[-1])
        with self.assertRaisesRegex(ValueError, "exactly the twelve ARM64"):
            self.recipe()

    def test_sparse_recipe_reconstructs_every_apk_byte(self):
        self.create_apk()
        # A marker before the central directory represents bytes the envelope
        # must retain, like a real APK signing block. No signature is simulated.
        with zipfile.ZipFile(self.apk) as archive:
            central_offset = archive.start_dir
        original = self.apk.read_bytes()
        marker = b"non-payload signing-block bytes retained verbatim"
        transformed = bytearray(original[:central_offset] + marker + original[central_offset:])
        eocd = transformed.rfind(b"PK\x05\x06")
        struct.pack_into("<I", transformed, eocd + 16, central_offset + len(marker))
        self.apk.write_bytes(transformed)
        recipe = self.recipe()
        output = self.root / "rebuilt.apk"
        prepare.reconstruct(recipe, self.envelope, self.native, output)
        self.assertEqual(self.apk.read_bytes(), output.read_bytes())
        self.assertIn(marker, self.envelope.read_bytes())
        self.assertIn(b"retained EOCD comment", self.envelope.read_bytes())
        self.assertEqual(recipe, json.loads(json.dumps(recipe)))

    def test_reconstruction_rejects_corrupt_envelope_and_cleans_partial_output(self):
        self.create_apk()
        recipe = self.recipe()
        value = bytearray(self.envelope.read_bytes())
        value[-1] ^= 1
        self.envelope.write_bytes(value)
        output = self.root / "rebuilt.apk"
        with self.assertRaisesRegex(ValueError, "Envelope SHA-256"):
            prepare.reconstruct(recipe, self.envelope, self.native, output)
        self.assertFalse(output.exists())
        self.assertFalse(output.with_name("rebuilt.apk.part").exists())

    def test_reconstruction_rejects_corrupt_library(self):
        self.create_apk()
        recipe = self.recipe()
        (self.native / prepare.LIBRARIES[-1]).write_bytes(b"damaged")
        with self.assertRaisesRegex(ValueError, "Library SHA-256"):
            prepare.reconstruct(recipe, self.envelope, self.native, self.root / "rebuilt.apk")

    def test_duplicate_or_gapped_segments_are_rejected(self):
        self.create_apk()
        recipe = self.recipe()
        bad = copy.deepcopy(recipe)
        bad["segments"][0]["offset"] = 1
        with self.assertRaisesRegex(ValueError, "overlap or have gaps"):
            prepare.validate_recipe(bad)
        bad = copy.deepcopy(recipe)
        bad["segments"].append(next(item for item in bad["segments"] if item["kind"] == "library"))
        with self.assertRaisesRegex(ValueError, "duplicate library"):
            prepare.validate_recipe(bad)

    def test_reordered_library_segments_fail_final_apk_integrity(self):
        self.create_apk()
        recipe = self.recipe()
        positions = [i for i, item in enumerate(recipe["segments"]) if item["kind"] == "library"]
        first, second = positions[:2]
        recipe["segments"][first], recipe["segments"][second] = recipe["segments"][second], recipe["segments"][first]
        with self.assertRaisesRegex(ValueError, "Reconstructed APK SHA-256"):
            prepare.reconstruct(recipe, self.envelope, self.native, self.root / "rebuilt.apk")
        self.assertFalse((self.root / "rebuilt.apk.part").exists())

    def test_manifest_retains_package_version_and_official_native_installation_flags(self):
        import xml.etree.ElementTree as xml
        root = xml.fromstring(prepare.manifest("com.example.qa", 42, 24, 37))
        android = "{http://schemas.android.com/apk/res/android}"
        self.assertEqual("clang21", root.attrib["split"])
        self.assertEqual("com.example.qa", root.attrib["package"])
        self.assertEqual("42", root.attrib[android + "versionCode"])
        self.assertEqual("true", root.attrib[android + "isFeatureSplit"])
        self.assertEqual("false", root.find("application").attrib[android + "hasCode"])
        self.assertEqual("true", root.find("application").attrib[android + "extractNativeLibs"])

    def test_certificate_requires_one_original_signer(self):
        self.assertEqual("b" * 64, prepare.certificate_from_verification("Signer #1 certificate SHA-256 digest: " + "b" * 64 + "\n"))
        with self.assertRaisesRegex(ValueError, "exactly one"):
            prepare.certificate_from_verification("unverified")
        with self.assertRaisesRegex(ValueError, "exactly one"):
            prepare.certificate_from_verification("\n".join("Signer #1 certificate SHA-256 digest: " + "b" * 64 for _ in range(2)))

    def test_official_archive_contract_matches_existing_feature_preparation(self):
        gradle = (Path(__file__).resolve().parents[2] / "clang21/build.gradle.kts").read_text(encoding="utf-8")
        self.assertIn(prepare.RELEASE_URL, gradle)
        for archive in prepare.ARCHIVES:
            self.assertIn(archive.name, gradle)
            self.assertIn(archive.sha256, gradle)


if __name__ == "__main__":
    unittest.main()
