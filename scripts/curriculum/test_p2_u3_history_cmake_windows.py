"""Non-Windows preflight regressions for the exact published U3 recovery demo."""
from __future__ import annotations

import copy
import unittest

import verify_p2_u3_history_cmake_windows as scenario


class PublishedHistoryExerciseTests(unittest.TestCase):
    def test_real_editor_and_seven_success_cases_are_authoritative(self):
        files, observed = scenario.published_project()
        self.assertEqual(len(files), 6)
        self.assertEqual(observed, scenario.oracle(False))
        self.assertEqual(scenario.oracle(False).count(" OK\n"), 7)
        self.assertTrue(scenario.oracle(False).endswith("Bateria: OK\n"))

    def test_failed_stage_is_independently_different_from_correct_stage(self):
        files, _ = scenario.published_project()
        broken, repaired = scenario.stages(files)
        self.assertNotEqual(broken[scenario.LIBRARY_SOURCE], files[scenario.LIBRARY_SOURCE])
        self.assertEqual(repaired[scenario.LIBRARY_SOURCE], files[scenario.LIBRARY_SOURCE])
        self.assertEqual(scenario.oracle(True).count(" FALLO\n"), 3)
        self.assertEqual(scenario.oracle(True).count(" OK\n"), 4)
        self.assertTrue(scenario.oracle(True).endswith("Bateria: FALLO\n"))
        self.assertNotEqual(scenario.sha(scenario.oracle(True)), scenario.sha(scenario.oracle(False)))

    def test_only_source_is_recovered_and_comment_persists(self):
        files, _ = scenario.published_project()
        original = copy.deepcopy(files)
        broken, fixed = scenario.stages(files)
        self.assertEqual(files, original)
        self.assertEqual(
            {path for path in files if files[path] != broken[path]},
            {scenario.ROOT_CMAKE, scenario.LIBRARY_SOURCE},
        )
        self.assertEqual(
            {path for path in files if files[path] != fixed[path]},
            {scenario.ROOT_CMAKE},
        )
        self.assertTrue(fixed[scenario.ROOT_CMAKE].endswith(scenario.QA_COMMENT))
        self.assertEqual(broken[scenario.ROOT_CMAKE], fixed[scenario.ROOT_CMAKE])

    def test_unexpected_source_is_not_a_valid_reproduction(self):
        original, _ = scenario.published_project()
        copied = dict(original)
        copied[scenario.LIBRARY_SOURCE] = copied[scenario.LIBRARY_SOURCE].replace(
            scenario.ORIGINAL_EXPR, "return 0;"
        )
        self.assertNotIn(scenario.ORIGINAL_EXPR, copied[scenario.LIBRARY_SOURCE])
        with self.assertRaises(ValueError):
            scenario.stages(copied)


if __name__ == "__main__":
    unittest.main(verbosity=2)
