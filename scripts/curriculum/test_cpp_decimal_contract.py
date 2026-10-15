"""Regresión del contrato curricular opcional de literal decimal exacto.

No usa ejecución C++ ni aproximación binaria como criterio de igualdad.
Ejecutar: python -X utf8 -m unittest discover -s scripts/curriculum -p test_cpp_decimal_contract.py
"""

import copy
import json
import random
import subprocess
import unittest
from decimal import Decimal

import validar_json_programacion as contract


class DecimalLiteralProfileTest(unittest.TestCase):
    def test_all_documented_equivalent_spellings(self):
        accepted = (
            "3", "3.0", "3.00", "3e0", "3E+0", "30e-1", "0.3e1", "3.",
            ".3e1", "03.0", "03e0", "0'3.0", "3.0'0", "3'0e-1",
            "0.0'3e2", "3e0'0", "3e-0'0", "3e+0'0", "300e-2",
        )
        for literal in accepted:
            with self.subTest(literal=literal):
                self.assertEqual(("3", "0"), contract.parse_cpp_decimal_literal(literal))
                self.assertEqual(("3", "0"), contract.parse_cpp_decimal_literal(" \t\n" + literal + "\r\n "))

    def test_near_values_remain_distinct_before_binary_approximation(self):
        for literal in ("0", "2", "4", "3.1", "3.9", "3e1", "3e-1",
                        "3.0000000000000001", "2.9999999999999999"):
            with self.subTest(literal=literal):
                value = contract.parse_cpp_decimal_literal(literal)
                self.assertIsNotNone(value)
                self.assertNotEqual(("3", "0"), value)

    def test_incomplete_or_malformed_tokens_rejected(self):
        rejected = (
            "", " ", "3e", "3e+", "3e-", "3..0", "3 .0", "3. 0", "3,0",
            "nombre", "true", '"3"', "'3'", "3 /* comentario */", "3//comentario",
            "1+2", "(3)", "+3", "-3", "3;", "3\n0", "3\t0", "3e 0",
            ".", ".e0", "3e0e0", "3e0.0", "３", "٣", "3\x00",
        )
        for literal in rejected:
            with self.subTest(literal=literal):
                self.assertIsNone(contract.parse_cpp_decimal_literal(literal))

    def test_bases_suffixes_and_integer_octal_not_enabled(self):
        for literal in ("03", "00", "0'3", "0'0", "0x3", "0X3", "0b11", "0B11",
                        "3u", "3U", "3l", "3L", "3ll", "3z", "3.0f", "3.0F",
                        "3.0l", "3.0L", "3e0f", "0x1.8p1", "3_km"):
            with self.subTest(literal=literal):
                self.assertIsNone(contract.parse_cpp_decimal_literal(literal))

    def test_digit_separator_must_be_between_digits(self):
        for literal in ("'3", "3'", "3''0", "3'.0", "3.'0", "3.0'", "3e'0",
                        "3e+'0", "3e0'", "3'e0", "3e0''0"):
            with self.subTest(literal=literal):
                self.assertIsNone(contract.parse_cpp_decimal_literal(literal))

    def test_unsuffixed_integer_must_fit_profile_int(self):
        self.assertEqual(("2147483647", "0"), contract.parse_cpp_decimal_literal("2'147'483'647"))
        self.assertIsNone(contract.parse_cpp_decimal_literal("2147483648"))
        self.assertIsNone(contract.parse_cpp_decimal_literal("9999999999999999999999"))
        self.assertEqual(("2147483648", "0"), contract.parse_cpp_decimal_literal("2147483648.0"))

    def test_zero_is_exact_independent_of_arbitrary_exponent(self):
        huge = "9" * 5000
        for literal in ("0", "0.0", ".0", "00.000", "000e0", "0e" + huge, "0.0e-" + huge):
            with self.subTest(length=len(literal)):
                self.assertEqual(("0", "0"), contract.parse_cpp_decimal_literal(literal))

    def test_large_exponent_arithmetic_without_power_or_host_int_conversion(self):
        huge = "9" * 5000
        carried = "1" + "0" * 5000
        self.assertEqual(("3", carried), contract.parse_cpp_decimal_literal("30e" + huge))
        self.assertEqual(("3", "-" + carried), contract.parse_cpp_decimal_literal("0.3e-" + huge))
        self.assertEqual(contract.parse_cpp_decimal_literal("30e" + huge),
                         contract.parse_cpp_decimal_literal("3e" + carried))
        self.assertEqual(contract.parse_cpp_decimal_literal("0.3e-" + huge),
                         contract.parse_cpp_decimal_literal("3e-" + carried))

    def test_maximum_length_is_accepted_and_overrun_is_technical(self):
        at_limit = "3." + "0" * (contract.DECIMAL_LITERAL_MAX_LENGTH - 2)
        self.assertEqual(("3", "0"), contract.parse_cpp_decimal_literal(at_limit))
        self.assertEqual(("3", "0"), contract.parse_cpp_decimal_literal(" " + at_limit + " "))
        with self.assertRaises(contract.DecimalLiteralResourceLimit):
            contract.parse_cpp_decimal_literal(at_limit + "0")
        with self.assertRaises(contract.DecimalLiteralResourceLimit):
            contract.parse_cpp_decimal_literal("😀" * (contract.DECIMAL_LITERAL_MAX_LENGTH // 2 + 1))

    def test_exact_decimal_normalization_matches_independent_bounded_oracle(self):
        generator = random.Random(20261012)
        for _ in range(250):
            whole = str(generator.randrange(100_000))
            fraction = f"{generator.randrange(100_000):05d}"
            exponent = generator.randrange(-50, 51)
            literal = f"{whole}.{fraction}e{exponent:+d}"
            # Decimal construction is exact and tuple access does not round.
            digits = Decimal(literal).as_tuple()
            coefficient = "".join(str(digit) for digit in digits.digits).lstrip("0")
            if not coefficient:
                expected = ("0", "0")
            else:
                stripped = coefficient.rstrip("0")
                expected = (stripped, str(digits.exponent + len(coefficient) - len(stripped)))
            with self.subTest(literal=literal):
                self.assertEqual(expected, contract.parse_cpp_decimal_literal(literal))


class DecimalAnswerContractTest(unittest.TestCase):
    def setUp(self):
        contract.ERRORS.clear()
        self.block = {"type": "fill_code", "id": "test", "answers": ["3", "3.0"],
                      "answerComparison": contract.DECIMAL_LITERAL_MODE}

    def tearDown(self):
        contract.ERRORS.clear()

    def validate(self, block):
        contract.ERRORS.clear()
        contract.validate_answer_contract(block, "numeric-test")
        return list(contract.ERRORS)

    def test_optional_mode_accepts_exactly_equal_declared_answers(self):
        self.block["answers"] += ["3.00", "3e0", "30e-1", "0.3e1", "3E+0", "3."]
        self.assertEqual([], self.validate(self.block))

    def test_absent_mode_preserves_arbitrary_legacy_answers(self):
        for kind in ("fill_code", "output_prediction", "inline_fill_code"):
            with self.subTest(kind=kind):
                block = {"type": kind, "answers": ["3", "3.0", '"a  b"', "1+2"]}
                self.assertEqual([], self.validate(block))

    def test_present_unknown_null_or_wrong_type_mode_is_technical_error(self):
        for selector in (None, "", "trimmed", "cpp_decimal_value", 7, True, [], {}):
            with self.subTest(selector=selector):
                errors = self.validate(dict(self.block, answerComparison=selector))
                self.assertTrue(errors)
                self.assertIn("configuración técnica inválida", errors[0])

    def test_mode_is_only_supported_for_fill_code(self):
        for kind in ("inline_fill_code", "output_prediction", "multiple_choice", "code"):
            with self.subTest(kind=kind):
                errors = self.validate(dict(self.block, type=kind))
                self.assertTrue(any("sólo corresponde a fill_code" in error for error in errors))
                self.assertNotIn("answerComparison", contract.FIELDS[kind])
        self.assertIn("answerComparison", contract.FIELDS["fill_code"])

    def test_answers_shape_is_configuration_error(self):
        for answers in (None, [], "3", [None], [3], [""], [" "], ["3", False]):
            with self.subTest(answers=answers):
                self.assertTrue(self.validate(dict(self.block, answers=answers)))
        missing = dict(self.block)
        missing.pop("answers")
        self.assertTrue(self.validate(missing))

    def test_invalid_canonical_or_alternative_is_configuration_error(self):
        for answers in (["3e", "3"], ["3", "03"], ["3", "+3"], ["3", "3.0f"]):
            with self.subTest(answers=answers):
                errors = self.validate(dict(self.block, answers=answers))
                self.assertTrue(any("fuera del perfil" in error for error in errors))

    def test_differing_quantity_in_declared_answers_is_configuration_error(self):
        for alternative in ("3.1", "3.9", "3.0000000000000001", "2.9999999999999999", "3e-1"):
            with self.subTest(alternative=alternative):
                errors = self.validate(dict(self.block, answers=["3", alternative]))
                self.assertTrue(any("exactamente la cantidad" in error for error in errors))

    def test_oversized_config_answer_reports_technical_limit(self):
        errors = self.validate(dict(self.block, answers=["3." + "0" * contract.DECIMAL_LITERAL_MAX_LENGTH]))
        self.assertTrue(any("límite técnico" in error for error in errors))

    def test_existing_output_and_inline_contracts_remain_validated(self):
        self.assertEqual([], self.validate({"type": "output_prediction", "answers": ["3"], "outputComparison": "exact"}))
        self.assertTrue(self.validate({"type": "output_prediction", "answers": ["3"], "outputComparison": None}))
        self.assertEqual([], self.validate({"type": "inline_fill_code", "answers": ["&&", "||"],
                                           "answerAlternatives": [["&&", "and"], ["||", "or"]]}))
        self.assertTrue(self.validate({"type": "inline_fill_code", "answers": ["&&", "||"],
                                       "answerAlternatives": [["&&", "and", "||", "or"]]}))


class NumericCurriculumPreservationTest(unittest.TestCase):
    relative = "app/src/main/assets/content/es/programming/cpp_fundamentals/errores_y_construccion_gradual/lesson_requisitos_casos_y_primera_version.json"

    @classmethod
    def setUpClass(cls):
        old = json.loads(subprocess.check_output(["git", "show", "HEAD:" + cls.relative], cwd=contract.ROOT, text=True, encoding="utf-8"))
        # HEAD can already contain the integrated selector. This fixture models
        # the contract before that optional field, without changing its answers.
        cls.previous = copy.deepcopy(old["pages"][2]["blocks"][4])
        cls.previous.pop("answerComparison", None)
        cls.current = contract.load(contract.ROOT / cls.relative)["pages"][2]["blocks"][4]

    def test_actual_activity_adds_only_selector_and_keeps_canonical_answers(self):
        self.assertEqual("conservar_preparacion", self.previous["id"])
        self.assertEqual(["3", "3.0"], self.previous["answers"])
        self.assertEqual(dict(self.previous, answerComparison=contract.DECIMAL_LITERAL_MODE), self.current)
        self.assertTrue(contract.preserves_previous_exercise(self.relative, 2, 4, self.previous, self.current))

    def test_same_selector_cannot_authorize_other_location_or_legacy_field_changes(self):
        candidate = dict(self.previous, answerComparison=contract.DECIMAL_LITERAL_MODE)
        self.assertNotIn("answerComparison", self.previous)
        self.assertNotEqual(self.previous, candidate)
        self.assertFalse(contract.preserves_previous_exercise(self.relative, 2, 3, self.previous, candidate))
        for key, replacement in (("id", "moved_id"), ("code", "___"), ("answers", ["3.0", "3"]),
                                 ("question", "otra consigna"), ("explanation", "otra explicación")):
            with self.subTest(field=key):
                changed = copy.deepcopy(candidate)
                changed[key] = replacement
                self.assertFalse(contract.preserves_previous_exercise(self.relative, 2, 4, self.previous, changed))


if __name__ == "__main__":
    unittest.main()
