"""Offline regression tests with tiny original fixtures; no model copies."""
from __future__ import annotations

from contextlib import redirect_stderr, redirect_stdout
import copy
import hashlib
import io
import json
from pathlib import Path
import tempfile
import unittest
from unittest.mock import patch

import prepare_english_ai_pack as pack


class EnglishPackVerifierTest(unittest.TestCase):
    def setUp(self) -> None:
        self.temporary = tempfile.TemporaryDirectory()
        self.addCleanup(self.temporary.cleanup)
        self.root = Path(self.temporary.name)
        self.assets = self.root / "assets"
        self.assets.mkdir()
        names = (
            "gb_encoder.onnx", "gb_decoder.onnx", "gb.json", "us_encoder.onnx",
            "us_decoder.onnx", "us.json", "pos.onnx", "pos.json",
        )
        self.artifacts = []
        for index, name in enumerate(names):
            payload = (f"fixture {index}\n".encode() if name.endswith(".onnx")
                       else json.dumps({"fixture": index}).encode())
            (self.assets / name).write_bytes(payload)
            self.artifacts.append(pack.Artifact(name, len(payload),
                                                hashlib.sha256(payload).hexdigest(),
                                                "https://example.invalid/resources/" + name))
        self.provenance = {
            "format": 1,
            "packName": "english_misaki",
            "artifactContract": "EnglishAuxResources.artifacts",
            "precision": "FP32",
            "totalArtifactBytes": sum(a.size for a in self.artifacts),
            "sources": [{"id": name, "license": license_name,
                         "sourceUrl": f"https://example.invalid/source/{name}",
                         "licenseDeclarationUrl": f"https://example.invalid/license/{name}"}
                        for name, license_name in pack.REQUIRED_SOURCES.items()],
            "artifacts": [{"fileName": a.name, "bytes": a.size, "sha256": a.sha256,
                           "source": "bart-gb" if a.name.startswith("gb") else
                           "bart-us" if a.name.startswith("us") else "spacy-pos"}
                          for a in self.artifacts],
            "notices": sorted(pack.REQUIRED_NOTICES),
        }
        self.write_provenance()
        for name in pack.REQUIRED_NOTICES:
            path = self.assets / name
            path.parent.mkdir(exist_ok=True)
            path.write_bytes(f"Original fixture notice for {name}\n".encode())
        metadata_pins = {name: hashlib.sha256((self.assets / name).read_bytes()).hexdigest()
                         for name in {*pack.REQUIRED_NOTICES, "provenance.json"}}
        self.pins = patch.dict(pack.AUDITED_METADATA_SHA256, metadata_pins, clear=True)
        self.pins.start()
        self.addCleanup(self.pins.stop)
        self.manifest = self.root / "EnglishAuxResources.kt"
        self.manifest.write_text(
            'const val release = "fixture-v1"\n'
            'const val downloadBase = "https://example.invalid/resources/$release"\n'
            + "\n".join(f'artifact("{a.name}", {a.size}L, "{a.sha256}")'
                        for a in self.artifacts)
            + '\nsourceUrl = "$downloadBase/$name"\n', encoding="utf-8")

    def write_provenance(self) -> None:
        (self.assets / "provenance.json").write_bytes(
            (json.dumps(self.provenance, indent=2) + "\n").encode("utf-8"))

    def verify(self) -> pack.VerificationResult:
        return pack.verify_staged_assets(self.assets, manifest=self.manifest)

    def snapshot(self) -> dict[str, bytes]:
        return {p.relative_to(self.assets).as_posix(): p.read_bytes()
                for p in self.assets.rglob("*") if p.is_file()}

    def test_complete_fixture_is_verified_and_untouched(self) -> None:
        before = self.snapshot()
        result = self.verify()
        self.assertEqual(pack.VerificationResult(8, sum(a.size for a in self.artifacts), 9), result)
        self.assertEqual(before, self.snapshot())
        self.assertEqual(result, pack.verify_staged(self.assets, self.artifacts))

    def test_staged_verification_is_offline(self) -> None:
        with patch("socket.socket", side_effect=AssertionError("network attempted")):
            self.verify()

    def test_binary_same_size_corruption_is_rejected_without_repair(self) -> None:
        path = self.assets / self.artifacts[0].name
        path.write_bytes(b"X" + path.read_bytes()[1:])
        before = self.snapshot()
        with self.assertRaisesRegex(ValueError, "SHA-256 mismatch"):
            self.verify()
        self.assertEqual(before, self.snapshot())

    def test_json_configuration_bytes_are_not_newline_normalized(self) -> None:
        path = self.assets / self.artifacts[2].name
        path.write_bytes(path.read_bytes() + b"\r\n")
        with self.assertRaisesRegex(ValueError, "Size mismatch"):
            self.verify()

    def test_truncated_resource_is_rejected(self) -> None:
        path = self.assets / self.artifacts[0].name
        path.write_bytes(path.read_bytes()[:-1])
        with self.assertRaisesRegex(ValueError, "Size mismatch"):
            self.verify()

    def test_missing_resource_is_rejected(self) -> None:
        (self.assets / self.artifacts[0].name).unlink()
        with self.assertRaisesRegex(ValueError, "Unexpected or missing"):
            self.verify()

    def test_extra_resource_is_rejected(self) -> None:
        (self.assets / "unlicensed.onnx").write_bytes(b"extra")
        with self.assertRaisesRegex(ValueError, "unlicensed.onnx"):
            self.verify()

    def test_missing_asset_directory_is_rejected(self) -> None:
        with self.assertRaisesRegex(ValueError, "asset directory"):
            pack.verify_staged(self.root / "absent", self.artifacts)

    def test_each_license_is_pinned_not_merely_present(self) -> None:
        for name in sorted(pack.REQUIRED_NOTICES):
            with self.subTest(name=name):
                path = self.assets / name
                original = path.read_bytes()
                path.write_bytes(b"Different but nonempty notice\n")
                with self.assertRaisesRegex(ValueError, "Audited provenance/license"):
                    self.verify()
                path.write_bytes(original)

    def test_missing_notice_is_rejected(self) -> None:
        (self.assets / "licenses/NOTICE.txt").unlink()
        with self.assertRaisesRegex(ValueError, "licenses/NOTICE.txt"):
            self.verify()

    def test_provenance_revision_changes_fail_even_if_structurally_valid(self) -> None:
        self.provenance["sources"][0]["revision"] = "unaudited"
        self.write_provenance()
        with self.assertRaisesRegex(ValueError, "provenance/license SHA-256"):
            self.verify()

    def test_text_crlf_is_allowed_and_preserved(self) -> None:
        for name in pack.AUDITED_METADATA_SHA256:
            path = self.assets / name
            path.write_bytes(path.read_bytes().replace(b"\n", b"\r\n"))
        before = self.snapshot()
        self.verify()
        self.assertEqual(before, self.snapshot())

    def test_text_bare_cr_is_not_silently_normalized(self) -> None:
        path = self.assets / "licenses/NOTICE.txt"
        path.write_bytes(path.read_bytes().replace(b"\n", b"\r"))
        with self.assertRaisesRegex(ValueError, "provenance/license SHA-256"):
            self.verify()

    def test_license_and_source_references_are_semantically_validated(self) -> None:
        cases = (
            (lambda p: p.update(precision="INT8"), "FP32"),
            (lambda p: p.update(totalArtifactBytes=0), "size differs"),
            (lambda p: p["artifacts"][0].update(sha256="0" * 64), "hashes differ"),
            (lambda p: p["artifacts"][0].update(source="unknown"), "Unattributed"),
            (lambda p: p["sources"][0].update(license="unlicensed"), "sources/licenses"),
            (lambda p: p["sources"][0].update(sourceUrl="http://example.invalid/a"), "references"),
            (lambda p: p["sources"][0].update(licenseDeclarationUrl="https://example.invalid/"), "references"),
            (lambda p: p["sources"].append(p["sources"][0]), "sources/licenses"),
            (lambda p: p["notices"].pop(), "notices"),
        )
        for modify, message in cases:
            with self.subTest(message=message):
                provenance = copy.deepcopy(self.provenance)
                modify(provenance)
                with self.assertRaisesRegex(ValueError, message):
                    pack.validate_provenance(provenance, self.artifacts)

    def test_contract_preserves_exact_sizes_hashes_and_https_sources(self) -> None:
        artifacts = pack.contract(self.manifest)
        self.assertEqual([(a.name, a.size, a.sha256) for a in self.artifacts],
                         [(a.name, a.size, a.sha256) for a in artifacts])
        self.assertTrue(all(a.url.startswith("https://example.invalid/resources/fixture-v1/")
                            for a in artifacts))

    def test_contract_rejects_missing_duplicate_or_wrong_kind_artifacts(self) -> None:
        original = self.manifest.read_text(encoding="utf-8")
        cases = (
            original.replace(f'artifact("{self.artifacts[0].name}"', 'disabled("ignored"'),
            original.replace(self.artifacts[0].name, self.artifacts[1].name),
            original.replace(self.artifacts[0].name, "unexpected.json"),
            original.replace("https://", "http://"),
        )
        for contents in cases:
            with self.subTest(contents=contents):
                self.manifest.write_text(contents, encoding="utf-8")
                with self.assertRaises(ValueError):
                    pack.contract(self.manifest)

    def test_linked_resource_is_rejected(self) -> None:
        path = self.assets / self.artifacts[0].name
        target = self.root / "external.onnx"
        target.write_bytes(path.read_bytes())
        path.unlink()
        try:
            path.symlink_to(target)
        except OSError as error:
            self.skipTest(f"Creating symlinks is unavailable: {error}")
        with self.assertRaisesRegex(ValueError, "Linked resource"):
            self.verify()

    def test_cli_verifies_without_modifying_assets(self) -> None:
        before = self.snapshot()
        output = io.StringIO()
        with redirect_stdout(output):
            code = pack.main(["--assets", str(self.assets), "--manifest", str(self.manifest), "--verify-staged"])
        self.assertEqual(0, code)
        self.assertIn("8 existing FP32 resources", output.getvalue())
        self.assertEqual(before, self.snapshot())

    def test_cli_reports_tampering_with_failure_exit_code(self) -> None:
        (self.assets / "licenses/NOTICE.txt").write_bytes(b"changed")
        error = io.StringIO()
        with redirect_stderr(error):
            code = pack.main(["--assets", str(self.assets), "--manifest", str(self.manifest), "--verify-staged"])
        self.assertEqual(1, code)
        self.assertIn("verification FAILED", error.getvalue())


if __name__ == "__main__":
    unittest.main()
