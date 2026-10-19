"""Scoped editorial revisions and optional presentation never relax answer identity."""

import copy
import json
import unittest

import validar_json_programacion as contract


class PendingPresentationContractTest(unittest.TestCase):
    def setUp(self):
        contract.ERRORS.clear()

    def emphasis(self, value, text="Un objeto conserva su valor."):
        contract.ERRORS.clear()
        contract.validate_paragraph_emphasis_contract(
            {"type": "paragraph", "text": text, "emphasis": value}, "fixture")
        return not contract.ERRORS

    def test_explicit_exact_emphasis_is_optional_and_never_rewrites_text(self):
        block = {"type": "paragraph", "text": "Un objeto conserva su valor."}
        original = copy.deepcopy(block)
        contract.validate_paragraph_emphasis_contract(block, "fixture")
        self.assertEqual([], contract.ERRORS)
        self.assertEqual(original, block)
        self.assertTrue(self.emphasis([{"text": "objeto"}, {"text": "valor"}]))
        self.assertTrue(self.emphasis([]))
        self.assertTrue(self.emphasis([{"text": "objeto", "occurrence": 1.0}]))

    def test_repeated_and_overlapping_selections_require_the_explicit_occurrence(self):
        self.assertFalse(self.emphasis([{"text": "objeto"}], "objeto y objeto"))
        self.assertTrue(self.emphasis([{"text": "objeto", "occurrence": 2}], "objeto y objeto"))
        self.assertTrue(self.emphasis([{"text": "aa", "occurrence": 2}], "aaa"))
        self.assertTrue(self.emphasis([{"text": "😀"}], "Un 😀 y valor"))

    def test_bad_emphasis_selectors_are_reported_without_using_answers(self):
        for value in (None, False, "objeto", {}, ["objeto"], [None], [{}],
                      [{"text": ""}], [{"text": " \n"}], [{"text": "missing"}],
                      [{"text": "objeto", "style": "bold"}]):
            with self.subTest(value=value):
                self.assertFalse(self.emphasis(value))
        for occurrence in (None, False, 0, -1, 1.5, 2, "1", {}, [], 10**50):
            with self.subTest(occurrence=occurrence):
                self.assertFalse(self.emphasis([{"text": "objeto", "occurrence": occurrence}]))

    def test_eight_pinned_explanations_match_the_reviewed_source_and_preserve_all_other_fields(self):
        lesson = "objetos_y_estado/lesson_caracteres_y_valores_logicos.json"
        locations = ((1, 5), (2, 3), (3, 3), (4, 3), (6, 2), (6, 3), (6, 5), (8, 5))
        source = json.loads((contract.CONTENT / lesson).read_text(encoding="utf-8-sig"))
        for page, block in locations:
            location = (lesson, page, block)
            kind, explanation = contract.EXPLANATION_CLARIFICATIONS[location]
            published = source["pages"][page - 1]["blocks"][block - 1]
            self.assertEqual((kind, explanation), (published["type"], published["explanation"]))
            old = {"type": kind, "id": "stable", "question": "Original question",
                   "code": "Original C++ source", "answers": ["original"],
                   "language": "cpp", "explanation": "Previous wording"}
            current = dict(old, **contract.FILL_CODE_REVISIONS.get(location, {}), explanation=explanation)
            relative = str((contract.CONTENT / lesson).relative_to(contract.ROOT))
            self.assertTrue(contract.preserves_previous_exercise(relative, page - 1, block - 1, old, current))
            for key in ("id", "type", "question", "code", "answers", "language", "explanation"):
                changed = dict(current, **{key: ["changed"] if key == "answers" else "CHANGED"})
                with self.subTest(page=page, block=block, changed=key):
                    self.assertFalse(contract.preserves_previous_exercise(relative, page - 1, block - 1, old, changed))
            self.assertFalse(contract.preserves_previous_exercise(relative, page - 1, block, old, current))

    def test_verified_conversion_alternatives_do_not_create_holes_or_license_new_answers(self):
        lesson = "objetos_y_estado/lesson_conversiones_y_deduccion.json"
        location = (lesson, 4, 6)
        alternatives = [["double", "float", "long double", "double long"]]
        self.assertEqual(alternatives, contract.INLINE_ANSWER_ALTERNATIVES[location])
        relative = str((contract.CONTENT / lesson).relative_to(contract.ROOT))
        old = {"type": "inline_fill_code", "question": "Same", "template": "static_cast<[]>(total)",
               "answers": ["double"]}
        current = dict(old, answerAlternatives=alternatives)
        contract.validate_answer_contract(current, "fixture")
        self.assertEqual([], contract.ERRORS)
        self.assertTrue(contract.preserves_previous_exercise(relative, 3, 5, old, current))
        for key, value in (("answers", ["float"]), ("template", "changed []"),
                           ("answerAlternatives", [["double", "int"]])):
            self.assertFalse(contract.preserves_previous_exercise(relative, 3, 5, old, dict(current, **{key: value})))
        self.assertFalse(contract.preserves_previous_exercise(relative, 3, 6, old, current))


if __name__ == "__main__":
    unittest.main()
