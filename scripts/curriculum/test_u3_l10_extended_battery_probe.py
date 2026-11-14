"""Independent original CMake U3L10 expansion and negative-case regressions."""
import os
import unittest
import u3_l10_extended_battery_probe as probe


def compiler():
    return os.environ.get("CXX", "cl" if os.name == "nt" else "g++")


class U3L10ExtendedTest(unittest.TestCase):
    def test_new_lower_boundary_detects_real_implementation_errors(self):
        result = probe.audit(probe.candidate_with_new_case((3, 3, 7), 59), compiler())
        self.assertTrue(result.accepted, result.reason)
        self.assertEqual(result.new_call, (3, 3, 7, 59))
        self.assertEqual(len(result.phases), 25)
        expected_labels = ["baseline"] + [
            f"original-{index}-{kind}"
            for index in range(1, len(probe.ORIGINAL) + 1)
            for kind in ("flip", "destination")
        ] + ["new-flip", "new-destination"]
        self.assertEqual([label for label, _ in result.phases], expected_labels)
        self.assertEqual(result.phases[0], ("baseline", 0))
        self.assertTrue(all(code == 1 for _, code in result.phases[1:]))

    def assert_missing_destination_rejected(self, case_number: int) -> None:
        files = probe.candidate_with_new_case((3, 3, 7), 59)
        original = "aceptado == aceptacion_esperada && obtenido == ajuste_esperado"
        defective = ("aceptado == aceptacion_esperada && "
                     f"(caso == {case_number} || obtenido == ajuste_esperado)")
        self.assertEqual(files[probe.BATTERY].count(original), 1)
        files[probe.BATTERY] = files[probe.BATTERY].replace(original, defective)
        result = probe.audit(files, compiler())
        self.assertFalse(result.accepted, result.reason)
        self.assertEqual(result.new_call, (3, 3, 7, 59))
        self.assertEqual(result.phases[0], ("baseline", 0))
        self.assertEqual(result.phases[-2], (f"original-{case_number}-flip", 1))
        self.assertEqual(result.phases[-1], (f"original-{case_number}-destination", 0))
        self.assertIn(f"Original case {case_number} missed its destination", result.reason)

    def test_original_accepted_case_must_compare_its_adjustment(self):
        # Case 1 accepts and writes +1. The bool remains true when only the
        # destination is damaged; keeping the new case correct cannot hide it.
        self.assert_missing_destination_rejected(1)

    def test_original_rejected_case_must_preserve_its_destination(self):
        # Case 7 rejects reversed bounds and must leave the caller's 77 intact.
        # An unchanged false result does not prove that conservation was tested.
        self.assert_missing_destination_rejected(7)

    def test_new_upper_boundary_is_equally_valid(self):
        result = probe.audit(probe.candidate_with_new_case((7, 3, 7), -13), compiler())
        self.assertTrue(result.accepted, result.reason)
        self.assertEqual(result.new_call, (7, 3, 7, -13))

    def test_original_eleven_cases_do_not_gain_an_extra_case_receipt(self):
        result = probe.audit(probe.published_files(), compiler())
        self.assertFalse(result.accepted)
        self.assertIn("exactly one new call", result.reason)

    def test_printing_only_a_new_label_does_not_count(self):
        files = probe.published_files()
        anchor = '    std::cout << "Fallos=" << fallos << "\\n";'
        files[probe.BATTERY] = files[probe.BATTERY].replace(
            anchor, '    std::cout << "Caso=12 OK\\n";\n' + anchor)
        result = probe.audit(files, compiler())
        self.assertFalse(result.accepted)
        self.assertIn("new call", result.reason)

    def test_additional_call_without_comparison_cannot_pass(self):
        files = probe.published_files()
        anchor = '    std::cout << "Fallos=" << fallos << "\\n";'
        files[probe.BATTERY] = files[probe.BATTERY].replace(
            anchor, "    int extra{59};\n"
                    "    (void)intentar_ajuste(3, 3, 7, extra);\n" + anchor)
        result = probe.audit(files, compiler())
        self.assertFalse(result.accepted)
        self.assertIn("did not compare", result.reason)

    def test_an_expected_value_computed_with_the_library_is_not_independent(self):
        files = probe.candidate_with_new_case((3, 3, 7), 59)
        actual = "if (!comprobar(12, 3, 3, 7, 59, true, 0))"
        calculated = ("if (!comprobar(12, 3, 3, 7, 59, true, "
                      "([&] { int a=59; (void)intentar_ajuste(3,3,7,a); return a; }())))")
        self.assertEqual(files[probe.BATTERY].count(actual), 1)
        files[probe.BATTERY] = files[probe.BATTERY].replace(actual, calculated)
        result = probe.audit(files, compiler())
        self.assertFalse(result.accepted)
        self.assertIn("exactly one new call", result.reason)

    def test_original_library_client_header_and_cmake_are_immutable(self):
        valid = probe.candidate_with_new_case((3, 3, 7), 59)
        for path in ("CMakeLists.txt", probe.LIBRARY,
                     "publico/control/ajuste.h", "app/consulta.cpp"):
            tampered = dict(valid)
            tampered[path] += "\n// unauthorized\n"
            with self.subTest(file=path):
                result = probe.audit(tampered, compiler())
                self.assertFalse(result.accepted)
                self.assertIn("changed", result.reason)

    def test_temporary_msvc_warning_translation_never_edits_student_cmake(self):
        source = probe.published_files()["CMakeLists.txt"]
        self.assertEqual(probe.qa_cmake_toolchain_options(source, "g++"), source)
        self.assertEqual(probe.qa_cmake_toolchain_options(source, "clang++"), source)
        adapted = probe.qa_cmake_toolchain_options(source, "cl")
        self.assertNotEqual(adapted, source)
        self.assertEqual(adapted.count("if(MSVC)"), 3)
        self.assertEqual(adapted.count("PRIVATE /W4"), 3)
        self.assertEqual(adapted.count("PRIVATE -Wall -Wextra -Wpedantic"), 3)
        self.assertEqual(probe.published_files()["CMakeLists.txt"], source)

    def test_no_mutation_identity_is_exposed_as_an_argument_or_environment(self):
        original = probe.published_files()[probe.LIBRARY]
        baseline = probe.instrument(original, "baseline")
        flip = probe.instrument(original, "flip", (3, 3, 7, 59))
        other = probe.instrument(original, "destination", (3, 3, 7, 59))
        self.assertEqual(len({baseline, flip, other}), 3)
        for source in (baseline, flip, other):
            self.assertIn(probe.TRACE, source)
            self.assertNotIn("getenv(", source)
            self.assertNotIn("LGAED_U3L10_MUTANT", source)
        for phase, target in (("baseline", (3, 3, 7, 59)),
                              ("flip", None), ("forged", None)):
            with self.subTest(phase=phase), self.assertRaises(ValueError):
                probe.instrument(original, phase, target)


if __name__ == "__main__":
    unittest.main()
