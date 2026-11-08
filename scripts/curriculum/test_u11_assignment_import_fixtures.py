"""Authenticate the published U11L20 21-to-25 history used by native import QA."""
from copy import deepcopy
import hashlib
import json
from pathlib import Path
import subprocess
import tempfile
import unittest
from unittest.mock import patch

import stable_state_layout as layout
import u11_assignment_editorial_fixture_source as fixture


class PublishedAssignmentImportFixtureTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.raw = {side: fixture.published_bytes(side) for side in ("before", "after")}
        cls.before, cls.after = (json.loads(cls.raw[side]) for side in ("before", "after"))

    def test_complete_published_pair_preserves_the_twenty_one_edge_prefix(self):
        fixture.validate_published_transition(self.before, self.after)
        self.assertEqual(21, len(self.before["stateLayout"]["editorial"]))
        self.assertEqual(25, len(self.after["stateLayout"]["editorial"]))
        self.assertEqual(self.before["stateLayout"]["editorial"], self.after["stateLayout"]["editorial"][:21])
        self.assertEqual("block_0003_0001", self.before["stateLayout"]["editorial"][-1]["id"])
        self.assertEqual(layout.validate(self.before), layout.validate(self.after))
        self.assertEqual(set(layout._blocks(self.before)), set(layout._blocks(self.after)))
        for identity, old in layout._blocks(self.before).items():
            with self.subTest(identity=identity):
                self.assertEqual(layout.state_fingerprint(old), layout.state_fingerprint(layout._blocks(self.after)[identity]))

    def test_only_the_six_public_fields_change_in_four_original_owners(self):
        old_blocks, new_blocks = layout._blocks(self.before), layout._blocks(self.after)
        changed = {(identity, field) for identity, old in old_blocks.items()
                   for field in old.keys() | new_blocks[identity].keys()
                   if old.get(field) != new_blocks[identity].get(field)}
        self.assertEqual({(change["identity"], change["field"]) for change in fixture.CHANGES.values()}, changed)
        self.assertEqual(6, len(changed))
        for identity, spec in fixture.BLOCKS.items():
            self.assertEqual(spec["before"], layout.fingerprint(old_blocks[identity]))
            self.assertEqual(spec["after"], layout.fingerprint(new_blocks[identity]))

    def test_raw_git_bytes_are_independently_pinned_and_not_a_candidate(self):
        for side in ("before", "after"):
            with self.subTest(side=side):
                self.assertEqual(fixture.PUBLISHED_SOURCE_HASHES[side], hashlib.sha256(self.raw[side]).hexdigest())
                with patch.object(fixture.subprocess, "check_output", return_value=self.raw[side] + b"\n"):
                    with self.assertRaisesRegex(ValueError, "pinned Git bytes"):
                        fixture.published_bytes(side)
                with patch.object(fixture.subprocess, "check_output", return_value=self.raw["after" if side == "before" else "before"]):
                    with self.assertRaisesRegex(ValueError, "pinned Git bytes"):
                        fixture.published_bytes(side)
        self.assertNotEqual(fixture.SOURCE_HASHES["es"], fixture.PUBLISHED_SOURCE_HASHES["before"])
        self.assertNotEqual(fixture.AFTER_SHA256, fixture.PUBLISHED_SOURCE_HASHES["after"])
        with self.assertRaises(ValueError):
            fixture.published_bytes("candidate")

    def test_rejects_rewritten_prefix_and_changes_outside_the_reviewed_fields(self):
        for mutation in ("prefix", "edge_owner", "missing_edge", "owner", "answer", "options", "code",
                         "wording", "family", "other_block", "unrelated_metadata"):
            altered = deepcopy(self.after)
            blocks = layout._blocks(altered)
            with self.subTest(mutation=mutation):
                if mutation == "prefix": altered["stateLayout"]["editorial"][0]["beforeSha256"] = "0" * 64
                elif mutation == "edge_owner": altered["stateLayout"]["editorial"][-1]["id"] = "block_0004_0002"
                elif mutation == "missing_edge": altered["stateLayout"]["editorial"].pop()
                elif mutation == "owner": blocks["block_0002_0003"]["id"] += "_different"
                elif mutation == "answer": blocks["block_0004_0001"]["answers"][0] = "another answer"
                elif mutation == "options": blocks["block_0004_0002"]["options"][0] += " changed"
                elif mutation == "code": blocks["block_0004_0001"]["code"] += "\n// changed"
                elif mutation == "wording": blocks["block_0004_0001"]["explanation"] += " Otra explicación."
                elif mutation == "family": altered["stateLayout"]["family"]["en"]["sha256"] = "0" * 64
                elif mutation == "other_block": blocks["block_0003_0001"]["text"] += " Cambio ajeno."
                else: altered["title"] += " changed"
                with self.assertRaises((layout.LayoutError, ValueError)):
                    fixture.validate_published_transition(self.before, altered)

    def test_published_export_keeps_exact_bytes_and_is_idempotent(self):
        with tempfile.TemporaryDirectory() as folder:
            output = Path(folder) / "published"
            with patch.object(fixture, "candidate", side_effect=AssertionError("Published history must not be reconstructed")), \
                    patch.object(fixture, "historical_bytes", side_effect=AssertionError("Wrong earlier publication")):
                manifest_path = fixture.export(output, published=True)
                original = {path.name: (path.read_bytes(), path.stat().st_mtime_ns) for path in output.iterdir()}
                self.assertEqual(manifest_path, fixture.export(output, published=True))
                self.assertEqual(original, {path.name: (path.read_bytes(), path.stat().st_mtime_ns) for path in output.iterdir()})
            manifest = json.loads(manifest_path.read_bytes())
            self.assertEqual("published-assignment-editorial-transition", manifest["kind"])
            for side, count in (("before", 21), ("after", 25)):
                self.assertEqual(self.raw[side], (output / manifest[side]["file"]).read_bytes())
                self.assertEqual(fixture.PUBLISHED_COMMITS[side], manifest[side]["commit"])
                self.assertEqual(fixture.PUBLISHED_SOURCE_HASHES[side], manifest[side]["sha256"])
                self.assertEqual(count, manifest[side]["editorialCount"])
            self.assertEqual(4, len(manifest["blocks"]))

    def test_export_rejects_missing_history_and_existing_different_files_before_writing(self):
        with tempfile.TemporaryDirectory() as folder:
            output = Path(folder) / "missing"
            with patch.object(fixture.subprocess, "check_output", side_effect=subprocess.CalledProcessError(128, "git show")):
                with self.assertRaises(subprocess.CalledProcessError):
                    fixture.export(output, published=True)
            self.assertFalse(output.exists())
            output = Path(folder) / "conflict"
            output.mkdir()
            retained = output / "assignment-after.json"
            retained.write_bytes(b"retained original evidence\n")
            with self.assertRaises(FileExistsError):
                fixture.export(output, published=True)
            self.assertEqual([retained], list(output.iterdir()))
            self.assertEqual(b"retained original evidence\n", retained.read_bytes())


if __name__ == "__main__":
    unittest.main()
