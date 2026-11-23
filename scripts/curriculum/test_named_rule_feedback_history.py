"""Regression for published formatting after the named-rule UI pilot."""
from __future__ import annotations

from copy import deepcopy
import hashlib
import json
from pathlib import Path
import tempfile
import unittest
from unittest import mock

import named_rule_feedback_fixture_source as fixtures


class NamedRuleFeedbackHistoryTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.payloads = fixtures.publication_payloads("50b4799")

    def test_complete_publications_keep_canonical_owners_and_editorial_history(self):
        documents = fixtures.verify_publications(self.payloads, "50b4799")
        old, current = documents["original"], documents["current"]
        self.assertEqual(46, len(old["stateLayout"]["editorial"]))
        self.assertEqual(56, len(current["stateLayout"]["editorial"]))
        self.assertEqual([], old["stateLayout"].get("sourceFormatRevisions", []))
        self.assertEqual(10, len(current["stateLayout"]["sourceFormatRevisions"]))
        original = fixtures.history.active_owners(old)[0]
        revised = fixtures.history.active_owners(current)[0]
        archives = {entry["id"]: entry["block"]
                    for entry in current["stateLayout"]["sourceFormatRevisions"]}
        for owner in (fixtures.FOCAL_OWNER, *fixtures.PREREQUISITES):
            with self.subTest(owner=owner):
                self.assertEqual(archives[owner], original[owner])
                self.assertNotEqual(original[owner], revised[owner])
                self.assertEqual(original[owner]["outputCheck"], revised[owner]["outputCheck"])
        # Both UI inputs are entire authored publications, never a replacement
        # visible block copied out of a sourceFormatRevisions snapshot.
        self.assertGreater(len(self.payloads["current"]), len(self.payloads["original"]))

    def test_export_is_byte_exact_and_keeps_both_distinct_publications(self):
        original = deepcopy(self.payloads)
        with tempfile.TemporaryDirectory(prefix="named-rule-history-") as directory:
            manifest = fixtures.write_fixtures(Path(directory), "50b4799")
            self.assertEqual({"original.json", "baseline.json", "current.json", "manifest.json"},
                             {path.name for path in Path(directory).iterdir()})
            self.assertEqual(manifest, json.loads((Path(directory) / "manifest.json").read_bytes()))
            self.assertEqual(["original", "current"], [item["name"] for item in manifest["scenarios"]])
            for item in manifest["scenarios"]:
                actual = (Path(directory) / item["file"]).read_bytes()
                self.assertEqual(actual, self.payloads[item["name"]])
                self.assertEqual(item["sha256"], hashlib.sha256(actual).hexdigest())
                self.assertEqual(item["commit"], fixtures.PUBLICATIONS[item["name"]].commit)
            self.assertEqual(self.payloads["baseline"], (Path(directory) / "baseline.json").read_bytes())
            self.assertEqual(manifest["baseline"]["sha256"],
                             hashlib.sha256(self.payloads["baseline"]).hexdigest())
        self.assertEqual(original, self.payloads)

    def test_changed_bytes_and_foreign_publications_are_rejected(self):
        for name in fixtures.PUBLICATIONS:
            for replacement in (self.payloads[name] + b"\n",
                                self.payloads["current" if name == "original" else "original"]):
                with self.subTest(publication=name):
                    altered = {**self.payloads, name: replacement}
                    with self.assertRaisesRegex(ValueError, "pinned complete publication"):
                        fixtures.verify_publications(altered, "50b4799")
        with mock.patch.object(fixtures.history, "publication_bytes", return_value=b"{}"):
            with self.assertRaisesRegex(ValueError, "pinned complete publication"):
                fixtures.publication_payloads()

    def test_current_asset_is_complete_and_not_frozen_to_the_reproduction_pin(self):
        actual = fixtures.asset_path().read_bytes()
        current = fixtures.publication_payloads()
        self.assertEqual(current["current"], actual)
        fixtures.verify_publications(current)
        document = json.loads(self.payloads["current"])
        changed = deepcopy(document)
        # A later source-only publication legitimately adds an eleventh
        # archive and another review of the already archived focal owner.
        owners = fixtures.history.active_owners(changed)[0]
        owners["block_0003_0002"]["code"] = "\n" + owners["block_0003_0002"]["code"]
        owners[fixtures.FOCAL_OWNER]["initialCode"] = "\n" + owners[fixtures.FOCAL_OWNER]["initialCode"]
        family = {"es": document}
        for locale, digest in (
            ("en", "41976c8efd968641b20fb0bc248b9b79740fcd099238c8176db9fe3c2bbab4f7"),
            ("fr", "cad1bde698b96fb63a7634bea91b470f075712c6edcc972afcc424dddf607a0c"),
        ):
            pin = fixtures.history.Publication(fixtures.PUBLICATIONS["current"].commit, digest)
            relative = fixtures.RELATIVE.replace("/es/", "/" + locale + "/", 1)
            raw = fixtures.history.publication_bytes(fixtures.ROOT, relative, pin)
            family[locale] = json.loads(fixtures.history.verify_publication_bytes(raw, pin))
        later = fixtures.state.prepare_v2(changed, "es", family, document)
        payload = (json.dumps(later, ensure_ascii=False, indent=2) + "\n").encode("utf-8")
        supplied = {**self.payloads, "current": payload}
        self.assertEqual(len(later["stateLayout"]["sourceFormatRevisions"]), 11)
        fixtures.verify_publications(supplied)
        with self.assertRaisesRegex(ValueError, "pinned complete publication"):
            fixtures.verify_publications(supplied, "50b4799")
        altered = deepcopy(later)
        block = fixtures.history.active_owners(altered)[0][fixtures.FOCAL_OWNER]
        block["initialCode"] += "\nint forged_format_token;\n"
        with self.assertRaises(fixtures.state.LayoutError):
            fixtures.verify_publications({**self.payloads, "current": json.dumps(altered).encode("utf-8")})

    def test_missing_git_history_never_falls_back_to_current_assets(self):
        with mock.patch.object(fixtures.history, "publication_bytes",
                               side_effect=FileNotFoundError("historical Git object unavailable")):
            with self.assertRaises(FileNotFoundError):
                fixtures.publication_payloads()

    def test_current_cannot_erase_the_authentic_baseline_history(self):
        # Both inputs below are individually authentic publications. The
        # current one must nevertheless retain the later published floor.
        rolled_back = {**self.payloads, "current": self.payloads["original"]}
        with self.assertRaises(fixtures.state.LayoutError):
            fixtures.verify_publications(rolled_back)
        altered = json.loads(self.payloads["current"])
        altered["stateLayout"]["sourceFormatRevisions"].pop()
        with self.assertRaises(fixtures.state.LayoutError):
            fixtures.verify_publications({**self.payloads, "current": json.dumps(altered).encode("utf-8")})
        with self.assertRaisesRegex(ValueError, "pinned complete publication"):
            fixtures.verify_publications({**self.payloads, "baseline": self.payloads["original"]})


if __name__ == "__main__":
    unittest.main()
