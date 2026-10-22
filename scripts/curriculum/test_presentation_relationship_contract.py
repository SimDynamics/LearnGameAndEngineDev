"""Presentation links cannot relax state, response or translation conservation."""

from copy import deepcopy
from pathlib import Path
import unittest
from unittest import mock

import stable_state_layout as layout
import validar_json_programacion as programming


def source():
    def compiler(text, checked=True):
        block = {"type": "cpp_compiler", "language": "cpp", "initialCode": text}
        if checked:
            block["outputCheck"] = {"cases": [{"standardInput": "", "outputs": [text]}]}
        return block

    return {"id": "cpp_fundamentals__test__presentation", "title": "Original title", "pages": [
        {"title": "A", "blocks": [compiler("first"), {"type": "heading", "text": "Reference A"},
                                   {"type": "paragraph", "text": "Original paragraph"},
                                   compiler("second")]},
        {"title": "B", "blocks": [compiler("third", False), {"type": "note", "text": "Reference B"},
                                   {"type": "code", "language": "cpp", "code": "original code"},
                                   {"type": "fill_code", "id": "original", "language": "cpp",
                                    "code": "[]", "answers": ["int"]}]}]}


def prepared(version=1):
    document = source()
    if version == 1:
        return layout.prepare(document)
    return layout.prepare_v2(document, "es", {"es": document})


def relations(document):
    document["referenceAfter"] = [
        {"activity": "block_0000_0000", "blocks": ["block_0000_0001", "block_0000_0002"]},
        {"activity": "block_0000_0003", "blocks": ["block_0001_0001", "block_0001_0002"]}]
    document["activityAfter"] = [
        {"activity": "block_0000_0003", "requires": ["block_0000_0000"]},
        {"activity": "block_0001_0000", "requires": ["block_0000_0000", "block_0000_0003"]}]
    return document


class PresentationRelationshipContractTest(unittest.TestCase):
    def test_valid_optional_links_preserve_all_blocks_and_storage_addresses(self):
        for version in (1, 2):
            old = prepared(version)
            current = relations(deepcopy(old))
            unchanged = deepcopy(current)
            self.assertEqual(layout.validate(old), layout.validate(current))
            layout.validate_transition(old, current)
            self.assertEqual(old["pages"], current["pages"])
            self.assertEqual(old["stateLayout"], current["stateLayout"])
            self.assertEqual(unchanged, current)
            self.assertEqual(layout.lineage(old), layout.lineage(current))

    def test_both_extensions_can_be_added_revised_or_removed_without_changing_the_origin(self):
        for version in (1, 2):
            old = relations(prepared(version))
            revised = deepcopy(old)
            revised["referenceAfter"][0]["blocks"] = ["block_0000_0002"]
            revised["activityAfter"][1]["requires"] = ["block_0000_0003"]
            layout.validate_transition(old, revised)
            removed = deepcopy(revised)
            del removed["referenceAfter"], removed["activityAfter"]
            layout.validate_transition(revised, removed)
            self.assertEqual(old["stateLayout"], removed["stateLayout"])

    def test_empty_lists_are_native_no_ops_even_without_a_layout(self):
        document = source()
        old = deepcopy(document)
        document.update(referenceAfter=[], activityAfter=[])
        self.assertEqual(layout.validate(old), layout.validate(document))
        self.assertEqual(document, layout.align_for_previous_validation(old, document))

    def test_nonempty_relationships_require_a_valid_layout_and_cannot_bootstrap_one(self):
        for field in layout.PRESENTATION_FIELDS:
            document = relations(source())
            del document[next(other for other in layout.PRESENTATION_FIELDS if other != field)]
            for operation in (layout.validate, layout.prepare):
                with self.subTest(field=field, operation=operation.__name__):
                    with self.assertRaisesRegex(layout.LayoutError, "stable state-layout"):
                        operation(document)
            with self.assertRaises(layout.LayoutError):
                layout.align_for_previous_validation(source(), document)
        document = relations(prepared())
        document["stateLayout"]["current"][0]["blocks"][0] = "forged"
        with self.assertRaises(layout.LayoutError):
            layout.validate(document)

    def test_invalid_optional_list_types_are_never_ignored(self):
        for field in layout.PRESENTATION_FIELDS:
            for value in (None, False, 0, "", {}, {"activity": "block_0000_0000"}):
                document = prepared()
                document[field] = value
                with self.subTest(field=field, value=value):
                    with self.assertRaises(layout.LayoutError):
                        layout.validate(document)

    def test_relationship_objects_have_only_their_two_declared_keys(self):
        for field, entries in (("referenceAfter", "blocks"), ("activityAfter", "requires")):
            original = relations(prepared())[field][0]
            for value in (None, False, [], "", {}, {"activity": original["activity"]},
                          {entries: original[entries]}, dict(original, unknown=True),
                          dict(original, **{("requires" if entries == "blocks" else "blocks"): []})):
                document = prepared()
                document[field] = [value]
                with self.subTest(field=field, value=value):
                    with self.assertRaises(layout.LayoutError):
                        layout.validate(document)

    def test_bad_ids_empty_entries_duplicate_activities_and_self_links_fail(self):
        for field, entries in (("referenceAfter", "blocks"), ("activityAfter", "requires")):
            original = relations(prepared())[field][0]
            for identity in (None, False, 1, [], {}, "", " space", "a/b", "missing", "a" * 129):
                for key in ("activity", entries):
                    document = prepared()
                    link = deepcopy(original)
                    link[key] = [identity] if key == entries else identity
                    document[field] = [link]
                    with self.subTest(field=field, key=key, identity=identity):
                        with self.assertRaises(layout.LayoutError):
                            layout.validate(document)
            for values in ([], None, False, "block_0000_0000", {},
                           [original[entries][0], original[entries][0]], [original["activity"]]):
                document = prepared()
                document[field] = [dict(original, **{entries: values})]
                with self.assertRaises(layout.LayoutError):
                    layout.validate(document)
            document = prepared()
            document[field] = [deepcopy(original), deepcopy(original)]
            with self.assertRaises(layout.LayoutError):
                layout.validate(document)

    def test_reference_blocks_are_presentation_only_and_cannot_be_shared_between_groups(self):
        for identity in ("block_0000_0003", "block_0001_0000", "block_0001_0003"):
            document = prepared()
            document["referenceAfter"] = [{"activity": "block_0000_0000", "blocks": [identity]}]
            with self.assertRaises(layout.LayoutError):
                layout.validate(document)
        document = relations(prepared())
        document["referenceAfter"][0]["blocks"].append("block_0001_0001")
        with self.assertRaisesRegex(layout.LayoutError, "Duplicate reference presentation"):
            layout.validate(document)

    def test_reference_source_and_activity_prerequisites_require_authored_output_checks(self):
        document = prepared()
        document["referenceAfter"] = [{"activity": "block_0001_0000", "blocks": ["block_0001_0001"]}]
        with self.assertRaisesRegex(layout.LayoutError, "authored output check"):
            layout.validate(document)
        for activity, prerequisite in (("block_0000_0002", "block_0000_0000"),
                                       ("block_0001_0000", "block_0000_0002"),
                                       ("block_0001_0003", "block_0000_0003")):
            document = prepared()
            document["activityAfter"] = [{"activity": activity, "requires": [prerequisite]}]
            with self.assertRaises(layout.LayoutError):
                layout.validate(document)
        document = prepared()
        document["activityAfter"] = [{"activity": "block_0000_0003", "requires": ["block_0001_0000"]}]
        with self.assertRaisesRegex(layout.LayoutError, "authored output check"):
            layout.validate(document)

    def test_prerequisites_must_precede_the_target_and_references_must_follow(self):
        document = prepared()
        document["referenceAfter"] = [{"activity": "block_0000_0003", "blocks": ["block_0000_0002"]}]
        with self.assertRaisesRegex(layout.LayoutError, "follow"):
            layout.validate(document)
        document = prepared()
        document["activityAfter"] = [{"activity": "block_0000_0000", "requires": ["block_0000_0003"]}]
        with self.assertRaisesRegex(layout.LayoutError, "precede"):
            layout.validate(document)

    def test_visual_positions_follow_the_validated_move_not_historical_coordinates(self):
        old = prepared()
        plan = deepcopy(old["stateLayout"]["current"])
        plan[0]["blocks"][0], plan[0]["blocks"][3] = plan[0]["blocks"][3], plan[0]["blocks"][0]
        moved = layout.reorder(old, plan)
        moved["activityAfter"] = [{"activity": "block_0000_0000", "requires": ["block_0000_0003"]}]
        moved["referenceAfter"] = [{"activity": "block_0000_0003", "blocks": ["block_0000_0002"]}]
        layout.validate_transition(old, moved)
        self.assertEqual((0, 3), layout.validate(moved)[0, 0])

    def test_unrelated_root_page_and_response_changes_still_fail_after_valid_metadata(self):
        for version in (1, 2):
            old = prepared(version)
            for key, value in (("title", "Changed"), ("id", "other"), ("randomFlag", True),
                               ("referenceAfterTypo", []), ("activityAfterTypo", [])):
                changed = relations(deepcopy(old))
                changed[key] = value
                with self.subTest(version=version, key=key):
                    with self.assertRaises(layout.LayoutError):
                        layout.validate_transition(old, changed)
            for field, value in (("initialCode", "CHANGED"), ("outputCheck", {"cases": []})):
                changed = relations(deepcopy(old))
                changed["pages"][0]["blocks"][0][field] = value
                with self.assertRaises(layout.LayoutError):
                    layout.validate_transition(old, changed)
            changed = relations(deepcopy(old))
            changed["pages"][0]["title"] = "Changed page"
            with self.assertRaisesRegex(layout.LayoutError, "Page metadata"):
                layout.validate_transition(old, changed)

    def test_origin_family_and_editorial_proofs_are_not_relaxed(self):
        for version in (1, 2):
            old = prepared(version)
            changed = relations(deepcopy(old))
            changed["stateLayout"]["origin"][0]["blocks"][0]["sha256"] = "0" * 64
            with self.assertRaises(layout.LayoutError):
                layout.validate_transition(old, changed)
        old = prepared(2)
        changed = relations(deepcopy(old))
        changed["stateLayout"]["family"]["es"]["sha256"] = "0" * 64
        with self.assertRaisesRegex(layout.LayoutError, "historical language family"):
            layout.validate_transition(old, changed)
        changed = relations(deepcopy(old))
        changed["pages"][0]["blocks"][2]["text"] = "Changed original paragraph"
        with self.assertRaisesRegex(layout.LayoutError, "before/after proof"):
            layout.validate_transition(old, changed)

    def test_invalid_previous_links_cannot_be_removed_to_skip_validation(self):
        old = relations(prepared())
        old["referenceAfter"][0]["blocks"] = ["missing"]
        with self.assertRaises(layout.LayoutError):
            layout.validate_transition(old, prepared())

    def test_programming_contract_reuses_the_same_validation_without_mutating_source(self):
        document = relations(prepared())
        before = deepcopy(document)
        programming.ERRORS.clear()
        programming.validate_lesson_state_contract(document, "fixture")
        self.assertEqual([], programming.ERRORS)
        self.assertEqual(before, document)
        document["activityAfter"][0]["requires"] = ["missing"]
        programming.validate_lesson_state_contract(document, "fixture")
        self.assertTrue(any("presentación" in error for error in programming.ERRORS))
        programming.ERRORS.clear()

    def test_repository_preflight_checks_links_even_without_a_layout_or_git(self):
        root = Path("in-memory-curriculum-root").resolve()
        lesson = root / layout.CONTENT_DIRECTORY / "en/programming/lesson_fixture.json"
        invalid = source()
        invalid["referenceAfter"] = False
        with mock.patch.object(Path, "is_dir", return_value=True), \
                mock.patch.object(Path, "rglob", return_value=iter([lesson])), \
                mock.patch.object(layout, "load", return_value=invalid):
            with self.assertRaisesRegex(layout.LayoutError, "Invalid presentation relationships"):
                layout.check_repository(root, None)


if __name__ == "__main__":
    unittest.main()
