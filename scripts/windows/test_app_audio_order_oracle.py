"""Reject the trace patterns hidden by the former filtered/deduplicated oracle."""
from pathlib import Path
import runpy
import unittest
from unittest.mock import patch

DRIVER = runpy.run_path(str(Path(__file__).with_name("test_app_audio_revision.py")))


class GenerationOrderOracleTest(unittest.TestCase):
    def setUp(self):
        self.lesson = DRIVER["LESSON"]
        self.page = lambda index: f"programming/{self.lesson}/{index}|es|ef_dora|KOKORO"
        self.marker = dict(pid=7, tick=100, generation=dict(scans=10, page=self.page(29)))

    def trace(self, turns):
        rows = [dict(pid=7, tick=101 + index, generation=dict(running=True, scans=scan, page=page))
                for index, (scan, page) in enumerate(turns)]
        collector = DRIVER["recorded_generation_order"]
        with patch.dict(collector.__globals__, observations=lambda root: rows):
            return collector(Path("unused"), self.marker)

    def verify(self, trace):
        DRIVER["verify_generation_order"](trace, self.marker)

    def test_old_page30_cancelling_is_retained_but_not_a_new_turn(self):
        trace = self.trace([(10, self.page(29)), (10, self.page(29)),
                            (11, self.page(2)), (12, self.page(4)), (13, self.page(7)), (14, self.page(29))])
        self.assertEqual([(row["scan"], row["page"]) for row in trace],
                         [(10, self.page(29)), (11, self.page(2)), (12, self.page(4)), (13, self.page(7)), (14, self.page(29))])
        self.assertTrue(trace[0]["obsolete"])
        self.verify(trace)

    def test_new_pass_page30_before_the_changed_pages_fails(self):
        with self.assertRaises(AssertionError):
            self.verify(self.trace([(11, self.page(29)), (12, self.page(2)),
                                    (13, self.page(4)), (14, self.page(7))]))

    def test_a_return_cannot_be_hidden_by_global_deduplication(self):
        for indices in ((2, 4, 2, 7), (2, 4, 7, 2)):
            with self.subTest(indices=indices), self.assertRaises(AssertionError):
                self.verify(self.trace([(11 + index, self.page(page)) for index, page in enumerate(indices)]))

    def test_an_unfiltered_foreign_identity_before_urgent_pages_fails(self):
        foreign = "math_physics/other_course__unit__lesson/2|es|ef_dora|KOKORO"
        trace = self.trace([(11, foreign), (12, self.page(2)), (13, self.page(4)), (14, self.page(7))])
        self.assertEqual(trace[0]["page"], foreign)
        with self.assertRaises(AssertionError):
            self.verify(trace)


if __name__ == "__main__":
    unittest.main()
