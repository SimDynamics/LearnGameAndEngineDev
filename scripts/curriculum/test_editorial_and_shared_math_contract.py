"""Exact editorial approvals and shared puzzle schemas keep technical guards."""

import copy
import hashlib
import json
import subprocess
import unittest
from functools import lru_cache

import validar_json_programacion as contract


@lru_cache(maxsize=None)
def historical_lesson(revision, lesson):
    relative = (contract.CONTENT / lesson).relative_to(contract.ROOT).as_posix()
    return json.loads(subprocess.check_output(
        ["git", "show", revision + ":" + relative], cwd=contract.ROOT))


def digest(text):
    return hashlib.sha256(text.encode("utf-8")).hexdigest()


class EditorialAndSharedMathContractTest(unittest.TestCase):
    def setUp(self):
        contract.ERRORS.clear()

    def published_math(self):
        found = []
        for path in contract.CONTENT.glob("*/lesson_*.json"):
            for pn, page in enumerate(contract.load(path)["pages"]):
                for bn, block in enumerate(page["blocks"]):
                    if block["type"] == "math_puzzle":
                        found.append((path, pn, bn, block))
        self.assertEqual(7, len(found), "The seven registered shared activities remain explicit.")
        return found

    def editorial_cases(self, before, after):
        cases = {}
        for key, transition in contract.APPROVED_EDITORIAL_REVISIONS.items():
            lesson, page, block, kind, field = key
            old = historical_lesson(before, lesson)["pages"][page - 1]["blocks"][block - 1]
            new = historical_lesson(after, lesson)["pages"][page - 1]["blocks"][block - 1]
            if (digest(old.get(field, "")), digest(new.get(field, ""))) == transition:
                self.assertEqual(kind, old["type"])
                self.assertEqual(kind, new["type"])
                cases[(lesson, page, block)] = (old, new)
        return cases

    def preserved(self, location, old, new):
        lesson, page, block = location
        relative = (contract.CONTENT / lesson).relative_to(contract.ROOT)
        return contract.preserves_previous_exercise(str(relative), page - 1, block - 1, old, new)

    def test_original_seventeen_apa_changes_pass_against_their_original_baseline(self):
        cases = self.editorial_cases("ad4f362", "879f5ce")
        self.assertEqual(17, len(cases))
        for location, (old, new) in cases.items():
            with self.subTest(location=location):
                self.assertEqual(["explanation"], [key for key in old.keys() | new.keys() if old.get(key) != new.get(key)])
                self.assertTrue(self.preserved(location, old, new))

    def test_current_eight_changes_pass_without_replacing_the_reference_commit(self):
        cases = self.editorial_cases("879f5ce", "14add7ae10ddac9e23d262c69b0fcb77d89272d0")
        self.assertEqual(8, len(cases))
        self.assertEqual(27, len(contract.APPROVED_EDITORIAL_REVISIONS))
        for location, (old, new) in cases.items():
            with self.subTest(location=location):
                differences = {key for key in old.keys() | new.keys() if old.get(key) != new.get(key)}
                self.assertTrue(differences <= {"question", "explanation"})
                self.assertTrue(self.preserved(location, old, new))

    def test_approved_text_never_licenses_different_code_answers_ids_or_locations(self):
        cases = self.editorial_cases("ad4f362", "879f5ce")
        cases.update(self.editorial_cases("879f5ce", "14add7ae10ddac9e23d262c69b0fcb77d89272d0"))
        for location, (old, new) in cases.items():
            for key in new.keys() - {"question", "explanation", "reviewFeedback"}:
                changed = copy.deepcopy(new)
                changed[key] = "UNAPPROVED CHANGE" if not isinstance(new[key], str) else new[key] + " UNAPPROVED"
                with self.subTest(location=location, key=key):
                    self.assertFalse(self.preserved(location, old, changed))
            changed = dict(new, addedTechnicalField="UNAPPROVED")
            self.assertFalse(self.preserved(location, old, changed))
            self.assertFalse(self.preserved((location[0], location[1], location[2] + 100), old, new))
            for key in ("question", "explanation"):
                if old.get(key) != new.get(key):
                    self.assertFalse(self.preserved(location, old, dict(new, **{key: new[key] + " UNAPPROVED"})))
                    self.assertFalse(self.preserved(location, dict(old, **{key: old[key] + " UNAPPROVED"}), new))

    def test_editorial_diagnostic_names_the_changed_field_instead_of_claiming_code_changed(self):
        message = contract.previous_exercise_difference(
            {"type": "output_prediction", "code": "same", "explanation": "old"},
            {"type": "output_prediction", "code": "same", "explanation": "new"})
        self.assertIn("explanation", message)
        self.assertNotIn("code", message)
        self.assertNotIn("respuesta", message)

    def test_all_seven_actual_shared_puzzles_pass_without_altering_content(self):
        for path, pn, bn, block in self.published_math():
            with self.subTest(lesson=path.name, page=pn + 1, block=bn + 1):
                previous = copy.deepcopy(block)
                contract.ERRORS.clear()
                contract.validate_math_puzzle_contract(block, "current original")
                self.assertEqual([], contract.ERRORS)
                self.assertEqual(previous, block)
                self.assertTrue(set(block) <= contract.FIELDS["math_puzzle"])
                self.assertIn("math_puzzle", contract.EXERCISES)

    def test_existing_math_nested_answers_ids_order_and_feedback_remain_protected(self):
        for path, pn, bn, block in self.published_math():
            relative = str(path.relative_to(contract.ROOT))
            for key in block:
                changed = copy.deepcopy(block)
                changed[key] = "UNAPPROVED"
                with self.subTest(lesson=path.name, key=key):
                    self.assertFalse(contract.preserves_previous_exercise(relative, pn, bn, block, changed))
            changed = copy.deepcopy(block)
            changed["responses"][0]["validator"]["expected"] = "UNAPPROVED"
            self.assertFalse(contract.preserves_previous_exercise(relative, pn, bn, block, changed))

    def assert_invalid(self, block):
        contract.ERRORS.clear()
        contract.validate_math_puzzle_contract(block, "malformed fixture")
        self.assertTrue(contract.ERRORS)

    def test_matching_rejects_unknown_missing_repeated_targets_and_bad_ids(self):
        block = next(block for _, _, _, block in self.published_math()
                     if block["responses"][0]["type"] == "math_matching_drag")
        response = block["responses"][0]
        expected = response["validator"]["expected"]
        first, second = list(expected)[:2]
        mutations = []
        for value in (None, [], {}, {first: expected[first]}, dict(expected, unknown="missing"),
                      dict(expected, **{first: "missing"}), dict(expected, **{first: expected[second]}),
                      dict(expected, **{first: []})):
            changed = copy.deepcopy(block)
            changed["responses"][0]["validator"]["expected"] = value
            mutations.append(changed)
        for key in ("sources", "targets"):
            for value in ([], "wrong", [None], [{"id": []}], [{"id": "same"}, {"id": "same"}]):
                changed = copy.deepcopy(block)
                changed["responses"][0][key] = value
                mutations.append(changed)
        for changed in mutations:
            self.assert_invalid(changed)

    def test_table_contract_rejects_missing_fields_wrong_validators_and_missing_dropdown_answers(self):
        block = next(block for _, _, _, block in self.published_math()
                     if block["responses"][0]["type"] == "math_multi_input")
        response = block["responses"][0]
        for key, value in (("fields", []), ("fields", "wrong"), ("validator", []),
                           ("validator", {"type": "fields", "fields": {}}),
                           ("validator", {"type": "matching", "expected": {}})):
            changed = copy.deepcopy(block)
            changed["responses"][0][key] = value
            self.assert_invalid(changed)
        field_id = response["fields"][0]["id"]
        for value in ("not a number", None, False, "NaN", "1/0", "1e400", []):
            changed = copy.deepcopy(block)
            changed["responses"][0]["validator"]["fields"][field_id] = {"type": "numeric", "expected": value}
            self.assert_invalid(changed)
        dropdown = {"type": "math_puzzle", "prompt": "Choose", "responses": [{
            "id": "a", "type": "math_multi_input", "fields": [
                {"id": "f", "type": "dropdown", "options": [{"id": "real"}]}],
            "validator": {"type": "fields", "fields": {"f": {"type": "text", "expected": "missing"}}}}]}
        self.assert_invalid(dropdown)

    def test_unknown_versions_response_families_and_bad_composition_are_not_silently_accepted(self):
        block = self.published_math()[0][3]
        for key, value in (("schemaVersion", True), ("version", 2), ("prompt", " "),
                           ("responses", []), ("responses", [None]), ("representations", "wrong"),
                           ("tools", [{}]), ("feedback", [])):
            changed = dict(block, **{key: value})
            self.assert_invalid(changed)
        for mutation in ("type", "id", "validator"):
            changed = copy.deepcopy(block)
            changed["responses"][0][mutation] = "unknown" if mutation != "id" else ""
            self.assert_invalid(changed)
        changed = copy.deepcopy(block)
        changed["responses"].append(copy.deepcopy(changed["responses"][0]))
        self.assert_invalid(changed)

    def test_finite_numeric_and_tolerance_guards_match_the_native_bounded_syntax(self):
        for value in ("12", "11.25", "-1/2", "50%", "1,5", "−3", "1e-3", 0, 1.25):
            self.assertTrue(contract.is_finite_math_number(value), value)
        for value in (None, True, {}, [], "", "1/0", "NaN", "Infinity", "1_000", "2+3", "1e400", "1e-1001"):
            self.assertFalse(contract.is_finite_math_number(value), value)
        for value in (-1, "NaN", None, True, [], "Infinity"):
            contract.ERRORS.clear()
            contract.validate_math_tolerance({"tolerance": value}, "fixture")
            self.assertTrue(contract.ERRORS)


if __name__ == "__main__":
    unittest.main()
