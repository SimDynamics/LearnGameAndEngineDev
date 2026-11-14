"""Compile the real U1 reduction objective with private second-call evidence."""
from __future__ import annotations

import os
import unittest

import u1_reduction_probe as probe


def compiler():
    return os.environ.get("CXX", "cl" if os.name == "nt" else "g++")


class ReductionProbeTest(unittest.TestCase):
    def test_reduced_main_exposes_real_state_reuse(self):
        result = probe.audit(probe.reduced_candidate(), compiler())
        self.assertTrue(result.accepted, result.reason)
        self.assertIn("Segundo=1", result.original_stdout)
        self.assertIn("Segundo=2", result.changed_stdout)

    def test_initial_unreduced_source_remains_insufficient(self):
        result = probe.audit(probe.published_source(), compiler())
        self.assertFalse(result.accepted)
        self.assertIn("reduction", result.reason)

    def test_literal_one_is_not_a_second_result(self):
        candidate = probe.reduced_candidate().replace(
            "    const int segundo = contar_altos({}, 2);",
            "    (void)contar_altos({}, 2);").replace(
            'std::cout << "Segundo=" << segundo',
            'std::cout << "Segundo=" << 1')
        result = probe.audit(candidate, compiler())
        self.assertFalse(result.accepted)
        self.assertIn("not reflected", result.reason)

    def test_preparation_call_is_essential(self):
        candidate = probe.reduced_candidate().replace(
            "    (void)contar_altos({2}, 2);\n", "")
        result = probe.audit(candidate, compiler())
        self.assertFalse(result.accepted)
        self.assertIn("cumulative calls", result.reason)

    def test_repaired_function_does_not_substitute_for_the_bug(self):
        candidate = probe.reduced_candidate().replace(
            "int acumulado{0};", "int acumulado{0};").replace(
            "    return acumulado;", "    return 0;")
        result = probe.audit(candidate, compiler())
        self.assertFalse(result.accepted)
        self.assertIn("function body", result.reason)

    def test_dependency_removed_must_not_be_accepted(self):
        candidate = probe.reduced_candidate().replace("#include <vector>\n", "")
        result = probe.audit(candidate, compiler())
        self.assertFalse(result.accepted)
        self.assertIn("dependencies", result.reason)

    def test_different_authored_message_is_accepted(self):
        candidate = probe.reduced_candidate().replace("Segundo=", "Consulta vacia: ")
        result = probe.audit(candidate, compiler())
        self.assertTrue(result.accepted, result.reason)
        self.assertIn("Consulta vacia: 1", result.original_stdout)


if __name__ == "__main__":
    unittest.main()
