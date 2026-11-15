"""Historical source formatting keeps exact published reading presentations.

The fixtures are generated through the real authoring APIs. No source lesson,
historical signature, language family or student account is rewritten.
"""
from __future__ import annotations

from copy import deepcopy
import hashlib
import json
from pathlib import Path
import tempfile
import unittest

import stable_state_layout as layout
import source_format_retirement_fixture_source as fixtures


class SourceFormatRetirementTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.documents, cls.manifest = fixtures.build_fixtures()
        cls.cases = {case["name"]: case for case in cls.manifest["cases"]}
        cls.invalid = {case["name"]: case for case in cls.manifest["invalid"]}

    def pair(self, name):
        case = self.cases[name]
        return self.documents[case["before"]], self.documents[case["after"]]

    def invalid_document(self, name):
        return deepcopy(self.documents[self.invalid[name]["file"]])

    def legacy_snapshot_copy(self, document):
        legacy = deepcopy(document)
        for snapshot in legacy["stateLayout"].get("retirementHistory", []):
            snapshot.pop("editorialPrefix", None)
        return legacy

    def assert_prefix_then_legacy_rejection(self, document, legacy_message, *,
            prefix_message="A retirement source differs from its owner's exact editorial prefix"):
        snapshots = document["stateLayout"]["retirementHistory"]
        self.assertTrue(snapshots)
        self.assertTrue(all("editorialPrefix" in snapshot for snapshot in snapshots))
        with self.assertRaisesRegex(layout.LayoutError, prefix_message):
            layout.validate(document)
        # Legacy standalone documents still exercise the original independent
        # guard. Removing provenance is never proposed as a new transition.
        legacy = self.legacy_snapshot_copy(document)
        with self.assertRaisesRegex(layout.LayoutError, legacy_message):
            layout.validate(legacy)

    def test_pinned_real_publication_keeps_every_original_owner_and_mark(self):
        published = fixtures.published()
        before, after = self.pair("source-format-retirement")
        self.assertEqual(published["id"], fixtures.U7_ID)
        original_blocks = layout._blocks(published)
        for identity, original in original_blocks.items():
            with self.subTest(owner=identity):
                self.assertEqual(layout._blocks(before)[identity], original)
                self.assertEqual(layout._blocks(after)[identity], original)
        self.assertEqual(set(layout._blocks(before)) - set(original_blocks), {fixtures.QA_RETIRED})
        self.assertEqual(set(layout._blocks(after)), set(original_blocks))
        for field in ("origin", "family", "editorial", "sourceFormatRevisions"):
            self.assertEqual(published["stateLayout"][field], before["stateLayout"][field])
            self.assertEqual(before["stateLayout"][field], after["stateLayout"][field])
        self.assertEqual(published["textSemantics"], before["textSemantics"])
        self.assertEqual(before["textSemantics"], after["textSemantics"])
        self.assertEqual(len(published["stateLayout"]["additions"]) + 1,
                         len(before["stateLayout"]["additions"]))
        self.assertEqual(before["stateLayout"]["additions"], after["stateLayout"]["additions"])

    def test_snapshot_is_the_complete_previous_presentation_in_both_orders(self):
        for name in ("source-format-retirement", "synthetic-format-first",
                     "synthetic-retire-first", "synthetic-successive"):
            with self.subTest(case=name):
                case = self.cases[name]
                after = self.documents[case["after"]]
                for record in case["history"]:
                    prior = self.documents[record["source"]]
                    snapshot = after["stateLayout"]["retirementHistory"][record["index"]]
                    self.assertEqual(snapshot["pages"], prior["pages"])
                    self.assertEqual(snapshot["current"], prior["stateLayout"]["current"])
                    self.assertEqual(snapshot["compilerPuzzlesRequired"],
                                     prior.get("compilerPuzzlesRequired", False))
                    self.assertEqual(snapshot["referenceAfter"], prior.get("referenceAfter", []))
                    self.assertEqual(snapshot["activityAfter"], prior.get("activityAfter", []))

    def test_all_active_addresses_and_immutable_families_survive(self):
        for case in self.manifest["cases"]:
            with self.subTest(case=case["name"]):
                before, after = self.documents[case["before"]], self.documents[case["after"]]
                old_positions, new_positions = layout.validate(before), layout.validate(after)
                old_addresses = {identity: old_positions[p, b]
                                 for p, page in enumerate(before["stateLayout"]["current"])
                                 for b, identity in enumerate(page["blocks"])}
                for p, page in enumerate(after["stateLayout"]["current"]):
                    for b, identity in enumerate(page["blocks"]):
                        self.assertEqual(old_addresses[identity], new_positions[p, b])
                for field in ("origin", "family", "additions"):
                    self.assertEqual(before["stateLayout"][field], after["stateLayout"][field])
                self.assertEqual(before.get("textSemantics"), after.get("textSemantics"))
                layout.validate_transition(before, after)

    def test_successive_formats_and_guidance_keep_both_exact_readings(self):
        _, after = self.pair("synthetic-successive")
        case = self.cases["synthetic-successive"]
        first = self.documents[case["history"][0]["source"]]
        second = self.documents[case["history"][1]["source"]]
        owner = "block_0000_0000"
        self.assertNotEqual(layout._blocks(first)[owner]["code"],
                            layout._blocks(second)[owner]["code"])
        self.assertEqual(fixtures.history_block(after, owner, 0), layout._blocks(first)[owner])
        self.assertEqual(fixtures.history_block(after, owner, 1), layout._blocks(second)[owner])
        compiler = "block_0000_0004"
        self.assertNotIn("explanation", fixtures.history_block(
            after, compiler, 0)["outputCheck"]["cases"][0])
        self.assertEqual(fixtures.history_block(after, compiler, 1),
                         layout._blocks(after)[compiler])
        self.assertEqual(after["stateLayout"]["sourceFormatRevisions"],
                         first["stateLayout"]["sourceFormatRevisions"])

    def test_old_grouped_order_and_later_formatted_lines_remain_distinct(self):
        before, after = self.pair("refined-historical-order")
        owner = "block_0000_0002"
        earlier = fixtures.history_block(after, owner, 0)
        later = fixtures.history_block(after, owner, 1)
        original = next(item["block"] for item in after["stateLayout"]["orderRefinements"]
                        if item["id"] == owner)
        self.assertEqual(earlier, original)
        self.assertEqual(len(earlier["fragments"]), 2)
        self.assertEqual(len(later["fragments"]), 4)
        self.assertEqual(later, layout._blocks(before)[owner])
        self.assertEqual(later, layout._blocks(after)[owner])
        self.assertEqual(earlier["correctOrder"], [0, 1])
        self.assertEqual(later["correctOrder"], [0, 1, 2, 3])

    def test_all_three_reviewed_diagnostics_retain_their_exact_objectives(self):
        before, after = self.pair("reviewed-diagnostic-history")
        for identity, diagnostic in (
            ("block_0014_0002", "u6-difference-unselected-definition"),
            ("block_0015_0002", "u6-difference-conflicting-return"),
            ("block_0017_0002", "u6-difference-missing-header"),
        ):
            with self.subTest(owner=identity):
                previous = layout._blocks(before)[identity]
                actual = layout._blocks(after)[identity]
                historical = fixtures.history_block(after, identity)
                self.assertEqual(actual, previous)
                self.assertEqual(historical, previous)
                self.assertEqual(actual["compileCheck"]["diagnostic"], diagnostic)
                archive = next(item["block"] for item in after["stateLayout"]["sourceFormatRevisions"]
                               if item["id"] == identity)
                self.assertNotEqual(historical["initialCode"], archive["initialCode"])
                self.assertEqual(historical["sourceFiles"], archive["sourceFiles"])
                self.assertEqual(historical["compileCheck"], archive["compileCheck"])

    def test_historical_diagnostic_cannot_borrow_another_goal_even_with_a_node(self):
        _, current = self.pair("reviewed-diagnostic-history")
        bad = deepcopy(current)
        owner = "block_0014_0002"
        old = fixtures.history_block(bad, owner)
        old["compileCheck"]["diagnostic"] = "explicit-assignment"
        fixtures._include_historical_node(bad, owner, old)
        with self.assertRaises(layout.LayoutError):
            layout.validate(bad)

    def test_closed_compiler_reference_versions_survive_all_three_orders(self):
        original = fixtures.published("compiler")
        original_compiler = layout._blocks(original)[fixtures.COMPILER]
        original_reference = layout._blocks(original)[fixtures.REFERENCE]
        for name, expanded in (
            ("original-before-expansion", False),
            ("expanded-before-formatting", True),
            ("formatted-expanded-history", True),
        ):
            with self.subTest(case=name):
                _, after = self.pair(name)
                old_compiler = fixtures.history_block(after, fixtures.COMPILER)
                old_reference = fixtures.history_block(after, fixtures.REFERENCE)
                if not expanded:
                    self.assertEqual(old_compiler, original_compiler)
                    self.assertEqual(old_reference, original_reference)
                else:
                    self.assertNotEqual(old_compiler["outputCheck"], original_compiler["outputCheck"])
                    self.assertNotEqual(old_reference["code"], original_reference["code"])
                self.assertEqual(
                    len(old_compiler["outputCheck"]["cases"][0]["outputs"][0].splitlines()),
                    30 if expanded else 19)
                layout.validate(after)

    def test_old_and_expanded_compiler_reference_cannot_be_mixed(self):
        _, after = self.pair("formatted-expanded-history")
        original = fixtures.published("compiler")
        for identity in (fixtures.COMPILER, fixtures.REFERENCE):
            with self.subTest(owner=identity):
                bad = deepcopy(after)
                block = fixtures.history_block(bad, identity)
                block.clear()
                block.update(deepcopy(layout._blocks(original)[identity]))
                self.assert_prefix_then_legacy_rejection(bad, "same published version")

    def test_unpublished_whitespace_is_not_a_historical_publication(self):
        bad = self.invalid_document("unpublished-whitespace")
        original = bad["stateLayout"]["sourceFormatRevisions"][0]["block"]
        historical = fixtures.history_block(bad, "block_0000_0000")
        self.assertTrue(layout.code_format.require_source_lexical(original, historical))
        self.assert_prefix_then_legacy_rejection(bad, "authenticated editorial source")

    def test_unpublished_guidance_cannot_hide_behind_compatible_source(self):
        bad = self.invalid_document("unpublished-guidance")
        self.assert_prefix_then_legacy_rejection(bad, "authenticated editorial source")

    def test_changed_token_is_rejected_after_its_full_digest_is_recomputed(self):
        bad = self.invalid_document("changed-token")
        identity = "block_0000_0000"
        snapshot = fixtures.history_block(bad, identity)
        nodes = {proof[field] for proof in bad["stateLayout"]["editorial"]
                 if proof["id"] == identity for field in ("beforeSha256", "afterSha256")}
        self.assertIn(layout.fingerprint(snapshot), nodes)
        self.assert_prefix_then_legacy_rejection(bad, "Unsafe historical source formatting",
            prefix_message="A retirement editorial prefix differs from its complete published history")

    def test_answers_oracles_order_and_project_paths_remain_protected(self):
        for name in ("changed-answer", "changed-oracle", "changed-order", "changed-project-path"):
            with self.subTest(mutation=name):
                self.assert_prefix_then_legacy_rejection(self.invalid_document(name), "response/code contract",
                    prefix_message="A retirement editorial prefix differs from its complete published history")

    def test_owner_archive_and_editorial_chain_cannot_be_substituted(self):
        for name in ("borrowed-owner", "changed-original-archive",
                     "missing-original-archive", "broken-editorial-chain"):
            with self.subTest(mutation=name), self.assertRaises(layout.LayoutError):
                layout.validate(self.invalid_document(name))

    def test_published_reading_snapshot_cannot_be_replaced_with_another_valid_version(self):
        _, after = self.pair("synthetic-format-first")
        bad = deepcopy(after)
        owner = "block_0000_0000"
        original = next(item["block"] for item in bad["stateLayout"]["sourceFormatRevisions"]
                        if item["id"] == owner)
        historical = fixtures.history_block(bad, owner)
        historical.clear()
        historical.update(deepcopy(original))
        # Both full blocks are real editorial nodes. Only the already published
        # retirement snapshot determines which one belongs to this reading.
        with self.assertRaisesRegex(layout.LayoutError,
                "A retirement source differs from its owner's exact editorial prefix"):
            layout.validate(bad)
        legacy_before = self.legacy_snapshot_copy(after)
        legacy_bad = self.legacy_snapshot_copy(bad)
        layout.validate(legacy_before)
        layout.validate(legacy_bad)
        with self.assertRaisesRegex(layout.LayoutError, "append-only"):
            layout.validate_transition(legacy_before, legacy_bad)

    def test_retiring_the_formatted_owner_itself_remains_outside_this_transition(self):
        before, _ = self.pair("synthetic-format-first")
        with self.assertRaisesRegex(layout.LayoutError, "inactive.*source format"):
            layout.retire(before, ["block_0000_0004"])

    def test_independent_source_hash_rejects_changed_bytes_for_every_real_pin(self):
        for name, (_, _, expected) in fixtures.SOURCES.items():
            with self.subTest(source=name):
                payload = fixtures.historical_bytes(name)
                self.assertEqual(hashlib.sha256(payload).hexdigest(), expected)
                with self.assertRaisesRegex(ValueError, "pinned publication"):
                    fixtures.verify_bytes(payload + b"\n", name)

    def test_export_manifest_contains_all_reading_sources_and_expected_rejections(self):
        with tempfile.TemporaryDirectory(prefix="lgaed-format-retirement-") as directory:
            folder = Path(directory)
            manifest = json.loads(fixtures.export_fixtures(folder).read_text(encoding="utf-8"))
            self.assertEqual(manifest, self.manifest)
            for case in manifest["cases"]:
                names = [case["before"], case["after"],
                         *(item["source"] for item in case["history"])]
                for name in names:
                    self.assertEqual(Path(name).name, name)
                    self.assertEqual(json.loads((folder / name).read_text(encoding="utf-8")),
                                     self.documents[name])
            for case in manifest["invalid"]:
                self.assertTrue((folder / case["file"]).is_file())


if __name__ == "__main__":
    unittest.main()
