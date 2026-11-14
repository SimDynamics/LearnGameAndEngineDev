"""Real C++ mutation checks for the two historic U1 open batteries.

The tests are isolated source-contract QA, not a substitute for Windows'
runtime completion gate. They do not edit the published course or student data.
"""
from __future__ import annotations

import os
import unittest

import u1_battery_mutation_oracle as probe


def compiler() -> str:
    return os.environ.get("CXX", "cl" if os.name == "nt" else "g++")


class U1OpenBatteryMutationTest(unittest.TestCase):
    def test_original_three_case_defect_and_all_independent_mutants(self):
        valid = probe.audit(probe.three_case_candidate(), probe.THREE, compiler(),
                            broken_original=True)
        self.assertTrue(valid.accepted, valid.reason)
        self.assertEqual(len(valid.phases), 5)
        initial = probe.source_for("block_0004_0004")
        self.assertFalse(probe.audit(initial, probe.THREE, compiler(),
                                     broken_original=True).accepted)

    def test_six_separate_checks_catch_new_equality_failure(self):
        good = probe.audit(probe.six_case_candidate(), probe.SIX, compiler(),
                           broken_original=False)
        self.assertTrue(good.accepted, good.reason)
        self.assertEqual(len(good.phases), 7)
        initial = probe.source_for("block_0005_0009")
        excluded = probe.audit(initial, probe.SIX, compiler(),
                               broken_original=False)
        self.assertFalse(excluded.accepted)
        self.assertIn("missing", excluded.reason)

    def test_short_circuit_cannot_skip_later_calls_after_a_failure(self):
        original = probe.source_for("block_0005_0009")
        old = """    bool correcto{true};
    if (!comprobar(4, 5, true)) { correcto = false; }
    if (!comprobar(5, 5, true)) { correcto = false; }
    if (!comprobar(6, 5, false)) { correcto = false; }
    if (!correcto) { return 1; }
    return 0;"""
        altered = """    const bool correcto =
        comprobar(4, 5, true) && comprobar(5, 5, true) &&
        comprobar(6, 5, false) && comprobar(7, 8, true) &&
        comprobar(8, 8, true) && comprobar(9, 8, false);
    return correcto ? 0 : 1;"""
        self.assertEqual(original.count(old), 1)
        rejected = probe.audit(original.replace(old, altered), probe.SIX, compiler(),
                               broken_original=False)
        self.assertFalse(rejected.accepted)
        self.assertIn("skipped", rejected.reason)

    def test_printing_six_calls_without_comparing_does_not_earn_credit(self):
        original = probe.source_for("block_0005_0009")
        source = original[:original.index("int main() {")] + """int main() {
    (void)debe_reponer(4, 5);
    (void)debe_reponer(5, 5);
    (void)debe_reponer(6, 5);
    (void)debe_reponer(7, 8);
    (void)debe_reponer(8, 8);
    (void)debe_reponer(9, 8);
    std::cout << "Fallos=0" << std::endl;
    return 0;
}
"""
        rejected = probe.audit(source, probe.SIX, compiler(), broken_original=False)
        self.assertFalse(rejected.accepted)
        self.assertIn("mutant", rejected.reason)

    def test_returning_failure_regardless_of_comparisons_is_not_evidence(self):
        candidate = probe.six_case_candidate()
        self.assertIn("return 0;", candidate)
        candidate = candidate.replace("    return 0;\n}", "    return 1;\n}")
        invalid = probe.audit(candidate, probe.SIX, compiler(), broken_original=False)
        self.assertFalse(invalid.accepted)
        self.assertIn("correct", invalid.reason)

    def test_original_student_function_cannot_be_changed(self):
        source = probe.three_case_candidate()
        self.assertEqual(source.count("return disponibles < minimo;"), 1)
        changed = source.replace("return disponibles < minimo;",
                                 "return disponibles <= minimo;")
        invalid = probe.audit(changed, probe.THREE, compiler(), broken_original=True)
        self.assertFalse(invalid.accepted)
        self.assertIn("function was changed", invalid.reason)


if __name__ == "__main__":
    unittest.main()
