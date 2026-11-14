"""Cross-compiler regressions for real CMake battery contracts in P2 U3L1."""
from __future__ import annotations

import os
import unittest

import u3_cmake_battery_probe as proof


def compiler() -> str:
    return os.environ.get("CXX", "cl" if os.name == "nt" else "g++")


class U3CMakeBatteryTest(unittest.TestCase):
    def test_envios_expansion_detects_all_independent_errors(self):
        result = proof.audit(proof.ENVios, proof.envios_candidate(), compiler())
        self.assertTrue(result.accepted, result.reason)
        self.assertEqual(len(result.phases), 9)
        self.assertEqual(result.phases[0], ("correct", 0))
        self.assertEqual(result.phases[-1], ("original-bug", 1))
        self.assertEqual(result.observed, (0, 4, 8, 20, 1, 5, 17))

    def test_lotes_new_battery_checks_all_four_inputs(self):
        result = proof.audit(proof.LOTES, proof.lotes_candidate(), compiler())
        self.assertTrue(result.accepted, result.reason)
        self.assertEqual(len(result.phases), 5)
        self.assertEqual(result.phases[0], ("correct", 0))
        self.assertEqual(result.observed, (0, 6, 7, 18))

    def test_original_four_case_envios_does_not_earn_credit(self):
        source = proof.published_files(proof.ENVios)
        result = proof.audit(proof.ENVios, source, compiler())
        self.assertFalse(result.accepted)
        self.assertIn("missing", result.reason)

    def test_original_empty_lotes_test_does_not_earn_credit(self):
        source = proof.published_files(proof.LOTES)
        result = proof.audit(proof.LOTES, source, compiler())
        self.assertFalse(result.accepted)
        self.assertIn("missing", result.reason)

    def test_exit_without_real_comparisons_fails_mutations(self):
        source = proof.lotes_candidate()
        target = source[proof.LOTES.test_file]
        start = target.index("bool comprobar(")
        source[proof.LOTES.test_file] = target[:start] + """int main() {
    (void)lotes_para(0);
    (void)lotes_para(6);
    (void)lotes_para(7);
    (void)lotes_para(18);
    std::cout << "Todos correctos" << std::endl;
    return 0;
}
"""
        result = proof.audit(proof.LOTES, source, compiler())
        self.assertFalse(result.accepted)
        self.assertIn("mutant", result.reason)
        self.assertIn("exit status", result.reason)

    def test_return_early_does_not_skip_the_remaining_envios_cases(self):
        source = proof.envios_candidate()
        old = "    if (!comprobar(1, 1)) { correcto = false; }"
        self.assertEqual(source[proof.ENVios.test_file].count(old), 1)
        source[proof.ENVios.test_file] = source[proof.ENVios.test_file].replace(
            old, "    if (!comprobar(1, 1)) { return 1; }")
        result = proof.audit(proof.ENVios, source, compiler())
        self.assertFalse(result.accepted)
        self.assertIn("mutant-1", result.reason)
        self.assertIn("skipped", result.reason)

    def test_expected_cannot_be_calculated_by_extra_library_call(self):
        source = proof.lotes_candidate()
        old = "if (!comprobar(7, 2))"
        self.assertEqual(source[proof.LOTES.test_file].count(old), 1)
        source[proof.LOTES.test_file] = source[proof.LOTES.test_file].replace(
            old, "if (!comprobar(7, lotes_para(7)))")
        result = proof.audit(proof.LOTES, source, compiler())
        self.assertFalse(result.accepted)
        self.assertIn("repeated", result.reason)

    def test_library_and_client_remain_the_published_shared_components(self):
        original = proof.published_files(proof.ENVios)
        good = proof.envios_candidate()
        self.assertEqual(set(original), set(good))
        self.assertEqual(
            {path for path in original if original[path] != good[path]},
            {proof.ENVios.test_file},
        )
        bad = dict(good)
        bad[proof.ENVios.library] = bad[proof.ENVios.library].replace(
            "piezas / 4", "(piezas + 3) / 4")
        result = proof.audit(proof.ENVios, bad, compiler())
        self.assertFalse(result.accepted)
        self.assertIn("shared library", result.reason)
        changed_graph = dict(good)
        changed_graph["CMakeLists.txt"] += "\n# Unauthorized project mutation\n"
        result = proof.audit(proof.ENVios, changed_graph, compiler())
        self.assertFalse(result.accepted)
        self.assertIn("CMake contract", result.reason)


if __name__ == "__main__":
    unittest.main(verbosity=2)
