"""Optional C++ review guidance preserves every existing answer and content field."""

import copy
import unittest

import validar_json_programacion as contract


class CppReviewFeedbackContractTest(unittest.TestCase):
    def setUp(self):
        contract.ERRORS.clear()

    def assert_valid(self, block):
        contract.ERRORS.clear()
        contract.validate_review_feedback_contract(block, "fixture")
        self.assertEqual([], contract.ERRORS)

    def assert_invalid(self, block):
        contract.ERRORS.clear()
        contract.validate_review_feedback_contract(block, "fixture")
        self.assertTrue(contract.ERRORS)

    def test_legacy_requires_no_new_field(self):
        for kind in contract.FIELDS:
            self.assert_valid({"type": kind})

    def test_choice_retains_string_options_and_parallel_reviews(self):
        block = self.choice()
        before = copy.deepcopy(block)
        self.assert_valid(block)
        self.assertEqual(before, block)
        self.assertIn("reviewFeedback", contract.FIELDS["multiple_choice"])

    def test_free_and_order_reviews_are_optional_nonempty_text(self):
        for kind in ("fill_code", "output_prediction", "order_code", "drag_fill_code"):
            self.assert_valid({"type": kind, "reviewFeedback": "Revisa el proceso.\nConserva tu borrador."})
            for value in (None, "", " \t\n", False, 1, [], {}, ["text"]):
                with self.subTest(kind=kind, value=value):
                    self.assert_invalid({"type": kind, "reviewFeedback": value})

    def test_malformed_choice_reviews_fail_even_for_unselected_entries(self):
        for value in (None, "review", False, 1, {}, [], ["one"],
                      ["one", "two", "three", "four"], ["one", "", "three"],
                      ["one", " \t", "three"], ["one", None, "three"], ["one", 2, "three"]):
            with self.subTest(value=value):
                block = self.choice()
                block["reviewFeedback"] = value
                self.assert_invalid(block)

    def test_options_are_not_migrated_into_objects(self):
        for options in (None, "options", [], ["one", 2, "three"], [{"text": "one"}, "two", "three"]):
            block = self.choice()
            block["options"] = options
            self.assert_invalid(block)

    def test_only_supported_types_accept_the_extension(self):
        for kind in contract.FIELDS:
            self.assertEqual(kind in contract.REVIEW_FEEDBACK_TYPES, "reviewFeedback" in contract.FIELDS[kind])
            if kind not in contract.REVIEW_FEEDBACK_TYPES:
                self.assert_invalid({"type": kind, "reviewFeedback": "review"})

    def test_guidance_does_not_read_or_validate_solution_fields(self):
        block = self.choice()
        block.update(correctIndex={}, answers=False, explanation="SOLUTION")
        self.assert_valid(block)

    def test_preservation_ignores_only_the_new_field_without_mutating_blocks(self):
        for kind in contract.REVIEW_FEEDBACK_TYPES:
            old = self.legacy(kind)
            current = copy.deepcopy(old)
            current["reviewFeedback"] = ["one", "two", "three"] if kind == "multiple_choice" else "Review the process."
            before = copy.deepcopy(current)
            self.assertTrue(self.preserved(old, current))
            self.assertEqual(before, current)
            old["reviewFeedback"] = current["reviewFeedback"]
            current["reviewFeedback"] = ["new", "two", "three"] if kind == "multiple_choice" else "Another process review."
            self.assertTrue(self.preserved(old, current))

    def test_answers_positions_and_existing_fields_remain_protected(self):
        for kind in contract.REVIEW_FEEDBACK_TYPES:
            old = self.legacy(kind)
            for field, value in (("id", "another_id"), ("type", "paragraph"), ("question", "different question"),
                                 ("explanation", "different success explanation")):
                current = copy.deepcopy(old)
                current[field] = value
                current["reviewFeedback"] = ["one", "two", "three"] if kind == "multiple_choice" else "Review."
                with self.subTest(kind=kind, field=field):
                    self.assertFalse(self.preserved(old, current))
        for kind, field, value in (("multiple_choice", "options", ["changed", "b", "c"]),
                                   ("multiple_choice", "correctIndex", 2),
                                   ("fill_code", "answers", ["other literal"]),
                                   ("output_prediction", "answers", ["different output"]),
                                   ("order_code", "correctOrder", [1, 2, 0])):
            old = self.legacy(kind)
            current = copy.deepcopy(old)
            current[field] = value
            current["reviewFeedback"] = ["one", "two", "three"] if kind == "multiple_choice" else "Review."
            with self.subTest(kind=kind, field=field):
                self.assertFalse(self.preserved(old, current))

    def test_review_on_unsupported_type_remains_part_of_content(self):
        for kind in ("inline_fill_code",):
            block = self.legacy(kind)
            changed = dict(block, reviewFeedback="Not supported")
            self.assertIs(block, contract.without_review_feedback(block))
            self.assertFalse(self.preserved(block, changed))

    def test_order_language_is_optional_and_validated_without_using_the_answer(self):
        for language in ("cpp", "c++", "cmake", " CMake "):
            contract.ERRORS.clear()
            contract.validate_code_presentation_contract({"type": "order_code", "language": language,
                                                          "correctOrder": "DO NOT READ"}, "fixture")
            self.assertEqual([], contract.ERRORS)
        for language in (None, "", "python", 1, {}, []):
            contract.ERRORS.clear()
            contract.validate_code_presentation_contract({"type": "order_code", "language": language}, "fixture")
            self.assertTrue(contract.ERRORS)

    def test_drag_explanation_is_optional_nonempty_success_text(self):
        for explanation in ("Una explicación posterior al acierto.",):
            contract.ERRORS.clear()
            contract.validate_code_presentation_contract({"type": "drag_fill_code", "explanation": explanation}, "fixture")
            self.assertEqual([], contract.ERRORS)
        for explanation in (None, "", "\t\n", False, 1, [], {}):
            contract.ERRORS.clear()
            contract.validate_code_presentation_contract({"type": "drag_fill_code", "explanation": explanation}, "fixture")
            self.assertTrue(contract.ERRORS)

    @staticmethod
    def choice():
        return {"type": "multiple_choice", "options": ["one", "two", "three"],
                "reviewFeedback": ["Review one.", "Review two.", "Review three."]}

    @staticmethod
    def legacy(kind):
        return {"type": kind, "id": "unchanged", "question": "original question", "explanation": "success",
                "options": ["a", "b", "c"], "correctIndex": 1, "answers": ["literal"], "correctOrder": [2, 0, 1]}

    @staticmethod
    def preserved(old, current):
        relative = "app/src/main/assets/content/es/programming/cpp_fundamentals/fixture/lesson_review.json"
        return contract.preserves_previous_exercise(relative, 0, 0, old, current)


if __name__ == "__main__":
    unittest.main()
