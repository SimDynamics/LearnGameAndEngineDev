"""Authoring and publication boundaries for exact copied edition lessons."""
from __future__ import annotations

from copy import deepcopy
import hashlib
import json
from pathlib import Path
from types import SimpleNamespace
import unittest
from unittest.mock import patch

import course_edition as edition
import edition_source_fixture_source as fixture
import stable_state_layout as state
import validar_json_programacion as structural


class EditionSourceAuthoringTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.original_bytes = fixture.published_bytes("u6l2")
        cls.original = state.parse_json(cls.original_bytes.decode("utf-8"))
        cls.source_path = fixture.PUBLICATIONS["u6l2"]["path"]
        cls.value = deepcopy(fixture.DESCRIPTOR)
        cls.authored = deepcopy(cls.original)
        cls.authored.pop("stateLayout")
        cls.authored["id"] = edition.unit_prefix(cls.value) + "funciones_y_contratos__declaraciones_y_firmas"
        cls.prepared = edition.prepare_edition_lesson(
            cls.authored, cls.value, source_document=cls.original, source_path=cls.source_path)

    def prepare(self, original=None, *, value=None, source_path=None):
        original = deepcopy(self.original if original is None else original)
        authored = deepcopy(original)
        authored.pop("stateLayout")
        authored["id"] = self.authored["id"]
        return edition.prepare_edition_lesson(authored, self.value if value is None else value,
            source_document=original, source_path=self.source_path if source_path is None else source_path)

    def test_fresh_addresses_keep_exact_owners_and_contracts(self):
        original, prepared = self.original, self.prepared
        self.assertEqual(original, json.loads(self.original_bytes))
        self.assertNotIn(edition.SOURCE_FIELD, original)
        self.assertNotIn("stateLayout", self.authored)
        self.assertEqual(prepared["stateLayout"]["current"], original["stateLayout"]["current"])
        self.assertEqual(set(prepared["stateLayout"]), {
            "version", "locale", "origin", "current", "family", "additions", "editorial"})
        self.assertEqual(set(prepared["stateLayout"]["family"]), {"es"})
        self.assertEqual(prepared["stateLayout"]["additions"], [])
        self.assertEqual(prepared["stateLayout"]["editorial"], [])
        original_positions, new_positions = state.validate(original), state.validate(prepared)
        self.assertTrue(any(position != stored for position, stored in original_positions.items()))
        self.assertTrue(all(position == stored for position, stored in new_positions.items()))
        owners = state._blocks(prepared)
        self.assertEqual(owners, state._blocks(original))
        for page in prepared["stateLayout"]["origin"]:
            for entry in page["blocks"]:
                self.assertEqual(entry["sha256"], state.fingerprint(owners[entry["id"]]))
        owner = "block_0014_0002"
        self.assertEqual(owners[owner]["compileCheck"]["diagnostic"], "u6-difference-unselected-definition")
        # This visible, already-formatted diagnostic is intentionally NOT
        # accepted as a detached legacy block. Only its explicit copy context
        # can retain the validated legacy goal under a new lesson namespace.
        with self.assertRaises(state.LayoutError):
            state.validate_compile_check_contract(owners[owner])
        self.assertNotEqual(prepared["id"], original["id"])

    def test_contextual_structural_check_and_unchanged_publication(self):
        prepared = self.prepared
        count = len(structural.ERRORS)
        for page, record in enumerate(prepared["stateLayout"]["current"]):
            for index, identity in enumerate(record["blocks"]):
                block = prepared["pages"][page]["blocks"][index]
                if "compileCheck" in block:
                    structural.validate_cpp_compile_check_contract(
                        block, identity, document=prepared, position=(page, index))
        self.assertEqual(structural.ERRORS[count:], [])
        state.validate_transition(prepared, deepcopy(prepared))
        self.assertEqual(state.align_for_previous_validation(prepared, prepared), prepared)
        projected = state.original_projection(prepared)
        self.assertEqual(projected, {key: value for key, value in prepared.items() if key != "stateLayout"})
        family = "programming/cpp_fundamentals/edition_es_1__funciones_y_contratos/lesson_declaraciones_y_firmas.json"
        relative = f"{edition.CONTENT_DIRECTORY}/es/{family}"
        state._validate_family_baseline(Path("."), fixture.PUBLISHED_COMMIT, family,
            [(relative, prepared)], {self.source_path}, {self.source_path: self.original},
            {prepared["id"]: {family}})

    def test_first_copy_does_not_grant_a_general_edition_exception(self):
        with self.assertRaisesRegex(ValueError, "exact complete initial block"):
            edition.prepare_edition_lesson(self.authored, self.value)
        with self.assertRaises(edition.CourseEditionError):
            edition.prepare_edition_lesson(self.original, self.value,
                source_document=self.original, source_path=self.source_path)
        for arguments in ({"source_document": self.original}, {"source_path": self.source_path}):
            with self.subTest(arguments=tuple(arguments)):
                with self.assertRaises(edition.CourseEditionError):
                    edition.prepare_edition_lesson(self.authored, self.value, **arguments)
        forged = deepcopy(self.prepared)
        forged.pop(edition.FIELD)
        with self.assertRaises(ValueError):
            state.validate(forged)

    def test_wrong_paths_locale_digest_and_nested_sources_are_rejected(self):
        paths = [
            self.source_path.replace("/es/", "/en/", 1),
            self.source_path.replace("/programming/", "/other/", 1),
            self.source_path.replace("/cpp_fundamentals/", "/other_course/", 1),
            self.source_path.replace("/funciones_y_contratos/", "/../funciones_y_contratos/", 1),
            self.source_path.replace("/", "\\"), "/" + self.source_path,
            self.source_path.replace("lesson_declaraciones", "lesson_otras_declaraciones"),
        ]
        for path in paths:
            with self.subTest(path=path):
                with self.assertRaises(edition.CourseEditionError):
                    self.prepare(source_path=path)
        changed = deepcopy(self.prepared)
        changed[edition.SOURCE_FIELD]["sourceDocumentSha256"] = "0" * 64
        with self.assertRaisesRegex(ValueError, "complete source digest"):
            state.validate(changed)
        changed = deepcopy(self.original)
        changed[edition.FIELD] = deepcopy(self.value)
        with self.assertRaisesRegex(edition.CourseEditionError, "legacy source"):
            self.prepare(changed)
        changed = deepcopy(self.original)
        changed[edition.SOURCE_FIELD] = {}
        with self.assertRaisesRegex(edition.CourseEditionError, "legacy source"):
            edition.prepare_edition_lesson(self.authored, self.value,
                source_document=changed, source_path=self.source_path)

    def test_original_closed_goal_is_validated_even_with_new_checksums(self):
        forged = deepcopy(self.original)
        owner = state._blocks(forged)["block_0014_0002"]
        owner["compileCheck"]["diagnostic"] = "u6-difference-conflicting-return"
        # prepare recomputes the full proof digest; the unchanged legacy
        # recognizer must still reject this different project contract.
        with self.assertRaisesRegex(ValueError, "exact complete initial block"):
            self.prepare(forged)
        changed = deepcopy(self.prepared)
        state._blocks(changed)["block_0014_0002"]["compileCheck"]["diagnostic"] = "u6-difference-conflicting-return"
        with self.assertRaisesRegex(ValueError, "only an exact copy"):
            state.validate(changed)

    def test_v1_rejects_own_edits_and_inherited_maps_explicitly(self):
        edits = []
        changed = deepcopy(self.prepared)
        changed["pages"][0]["blocks"][0]["text"] += " QA"
        edits.append(changed)
        changed = deepcopy(self.prepared)
        state._blocks(changed)["block_0014_0002"]["initialCode"] += "\n"
        edits.append(changed)
        for changed in edits:
            with self.assertRaisesRegex(ValueError, "only an exact copy"):
                state.validate_transition(self.prepared, changed)
            with self.assertRaisesRegex(ValueError, "only an exact copy"):
                state.prepare_v2(changed, "es", {"es": self.prepared}, self.prepared)
        for field, value in (
                ("sourceFormatRevisions", deepcopy(self.original["stateLayout"]["sourceFormatRevisions"])),
                ("editorial", deepcopy(self.original["stateLayout"]["editorial"])),
                ("retirementHistory", [])):
            with self.subTest(field=field):
                changed = deepcopy(self.prepared)
                changed["stateLayout"][field] = value
                with self.assertRaisesRegex(ValueError, "fresh exact map"):
                    state.validate(changed)
        changed = deepcopy(self.prepared)
        # Positional labels would silently break existing root references.
        literal = edition.literal_layout(changed, pages=state._pages, state_fingerprint=state.fingerprint)
        changed["stateLayout"].update(literal)
        changed["stateLayout"]["family"]["es"]["origin"] = deepcopy(literal["origin"])
        with self.assertRaisesRegex(ValueError, "fresh exact map"):
            state.validate(changed)

    def test_publication_matches_real_git_source_and_retained_bytes(self):
        retained = SimpleNamespace(read_bytes=lambda: self.original_bytes)
        with patch.object(edition, "_git_bytes", return_value=self.original_bytes) as read, \
                patch.object(edition, "_edition_file", return_value=retained):
            edition.authenticate_source_publication(Path("."), fixture.PUBLISHED_COMMIT,
                self.prepared, previous_paths={self.source_path})
            read.assert_called_once_with(Path("."), fixture.PUBLISHED_COMMIT, self.source_path)
            with self.assertRaisesRegex(edition.CourseEditionError, "already published"):
                edition.authenticate_source_publication(Path("."), fixture.PUBLISHED_COMMIT,
                    self.prepared, previous_paths=set())
            # A self-consistent checksum and a valid legacy lesson alone do
            # not prove publication. The exact base resource remains required.
            invented_source = deepcopy(self.original)
            invented_source["title"] += " QA"
            for field in invented_source["textSemantics"].get("rootFields", []):
                if field["path"] == "/title":
                    field["sourceSha256"] = hashlib.sha256(invented_source["title"].encode("utf-8")).hexdigest()
            invented_copy = self.prepare(invented_source)
            state.validate(invented_copy)
            with self.assertRaisesRegex(edition.CourseEditionError, "verified Git base"):
                edition.authenticate_source_publication(Path("."), fixture.PUBLISHED_COMMIT,
                    invented_copy, previous_paths={self.source_path})
        with patch.object(edition, "_git_bytes", return_value=self.original_bytes), \
                patch.object(edition, "_edition_file", return_value=SimpleNamespace(read_bytes=lambda: b"changed")):
            with self.assertRaisesRegex(edition.CourseEditionError, "retain its exact published"):
                edition.authenticate_source_publication(Path("."), fixture.PUBLISHED_COMMIT,
                    self.prepared, previous_paths={self.source_path})

    def test_publication_destination_keeps_the_source_curriculum_route(self):
        target = (f"{edition.CONTENT_DIRECTORY}/es/programming/cpp_fundamentals/"
                  "edition_es_1__funciones_y_contratos/lesson_declaraciones_y_firmas.json")
        retained = SimpleNamespace(read_bytes=lambda: self.original_bytes)
        with patch.object(edition, "_git_bytes", return_value=self.original_bytes), \
                patch.object(edition, "_edition_file", return_value=retained):
            edition.authenticate_source_publication(Path("."), fixture.PUBLISHED_COMMIT,
                self.prepared, previous_paths={self.source_path}, target_path=target)
            for changed in (target.replace("/programming/", "/math_physics/"),
                            target.replace("/es/", "/fr/"),
                            target.replace("/cpp_fundamentals/", "/other_course/")):
                with self.subTest(target=changed):
                    with self.assertRaisesRegex(edition.CourseEditionError, "exact locale, category and course"):
                        edition.authenticate_source_publication(Path("."), fixture.PUBLISHED_COMMIT,
                            self.prepared, previous_paths={self.source_path}, target_path=changed)


if __name__ == "__main__":
    unittest.main()
