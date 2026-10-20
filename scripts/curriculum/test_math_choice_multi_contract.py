"""Strict curricular choice contracts mirror already supported native responses."""

import copy
import json
import unittest

import validar_json_programacion as contract


class MathChoiceMultiContractTest(unittest.TestCase):
    def fixture(self, expected=None, kind="math_multiple_choice_multi"):
        return {
            "type": "math_puzzle", "schemaVersion": 1, "id": "choice_fixture",
            "prompt": "Selecciona las formas que cumplen el requisito.",
            "representations": [], "tools": [],
            "responses": [{
                "id": "forms", "type": kind,
                "options": [
                    {"id": "a", "label": "Primera forma", "reviewFeedback": "Revisa la primera condición."},
                    {"id": "b", "label": "Segunda forma"},
                    {"id": "c", "text": "Tercera forma"},
                ],
                "validator": {"type": "choice", "expected": ["a", "c"] if expected is None else expected},
            }],
            "feedback": {"correct": "Las formas elegidas cumplen las condiciones.", "incorrect": "Revisa la selección."},
        }

    def errors(self, value):
        contract.ERRORS.clear()
        contract.validate_math_puzzle_contract(value, "choice fixture")
        return list(contract.ERRORS)

    def test_multi_accepts_one_several_all_and_any_expected_order_without_mutation(self):
        for expected in (["a"], ["a", "c"], ["c", "a"], ["a", "b", "c"]):
            value = self.fixture(expected)
            before = copy.deepcopy(value)
            with self.subTest(expected=expected):
                self.assertEqual([], self.errors(value))
                self.assertEqual(before, value)

    def test_multi_rejects_empty_non_text_unknown_and_repeated_expected_ids_without_crashing(self):
        for expected in (None, [], "", "a", {}, [None], [True], [1], [""], [" "],
                         ["missing"], ["a", "missing"], ["a", "a"], [[]], [{}]):
            value = self.fixture()
            value["responses"][0]["validator"]["expected"] = expected
            with self.subTest(expected=expected):
                self.assertTrue(self.errors(value))

    def test_multi_requires_expected_array_and_exact_choice_validator_family(self):
        value = self.fixture()
        del value["responses"][0]["validator"]["expected"]
        self.assertTrue(self.errors(value))
        for validator in ({}, [], "choice", None, {"type": "text", "expected": "a"},
                          {"type": "fields", "fields": {}}, {"type": "unknown", "expected": ["a"]}):
            value = self.fixture()
            value["responses"][0]["validator"] = validator
            with self.subTest(validator=validator):
                self.assertTrue(self.errors(value))

    def test_options_require_nonempty_unique_text_ids_and_existing_expected_members(self):
        for options in (None, [], "a", [None], [{}], [{"id": ""}], [{"id": " "}],
                        [{"id": True}], [{"id": 1}], [{"id": []}],
                        [{"id": "a"}, {"id": "a"}], [{"id": "a"}, {"id": "b"}]):
            value = self.fixture()
            value["responses"][0]["options"] = options
            with self.subTest(options=options):
                self.assertTrue(self.errors(value))

    def test_optional_review_feedback_is_a_nonblank_string_when_present(self):
        for review in (None, "", " \n", [], {}, True, 1):
            value = self.fixture()
            value["responses"][0]["options"][0]["reviewFeedback"] = review
            with self.subTest(review=review):
                self.assertTrue(self.errors(value))
        value = self.fixture()
        del value["responses"][0]["options"][0]["reviewFeedback"]
        self.assertEqual([], self.errors(value))

    def test_single_still_requires_exactly_one_expected_option(self):
        self.assertEqual([], self.errors(self.fixture(["b"], "math_multiple_choice_single")))
        for expected in ([], ["a", "c"], ["a", "a"], ["missing"]):
            with self.subTest(expected=expected):
                self.assertTrue(self.errors(self.fixture(expected, "math_multiple_choice_single")))

    def test_response_ids_unknown_families_and_versions_keep_existing_guards(self):
        value = self.fixture()
        value["responses"].append(copy.deepcopy(value["responses"][0]))
        self.assertTrue(self.errors(value))
        for key, new in (("id", ""), ("id", []), ("type", "unknown")):
            value = self.fixture()
            value["responses"][0][key] = new
            with self.subTest(key=key, new=new):
                self.assertTrue(self.errors(value))
        for version in (True, 2, "1"):
            value = self.fixture()
            value["schemaVersion"] = version
            self.assertTrue(self.errors(value))

    def test_actual_explicit_classification_passes_and_its_answers_stay_protected(self):
        relative = "datos_propios_y_estado_valido/lesson_valores_por_defecto_con_significado.json"
        path = contract.CONTENT / relative
        value = json.loads(path.read_text(encoding="utf-8-sig"))["pages"][13]["blocks"][5]
        self.assertEqual("math_puzzle", value["type"])
        self.assertEqual("cpp_fundamentals__datos_propios_y_estado_valido__valores_por_defecto_con_significado__clasificar_conversiones_implicitas", value["id"])
        self.assertEqual(["copia_numero", "argumento_numero"], value["responses"][0]["validator"]["expected"])
        self.assertEqual([], self.errors(value))
        previous = copy.deepcopy(value)
        location = str(path.relative_to(contract.ROOT))
        for target in ("id", "options", "validator"):
            changed = copy.deepcopy(value)
            if target == "id":
                changed["responses"][0]["id"] = "different_response"
            elif target == "options":
                changed["responses"][0]["options"].reverse()
            else:
                changed["responses"][0]["validator"]["expected"] = ["directa"]
            with self.subTest(target=target):
                self.assertFalse(contract.preserves_previous_exercise(location, 13, 5, previous, changed))
        self.assertEqual(previous, value)


if __name__ == "__main__":
    unittest.main()
