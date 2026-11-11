"""Diagnostic regression for immutable, locale-owned curricular origins.

A mismatch must remain rejected. Detail helps Work locate its historical
publication; this test never rewrites either origin or approves a baseline.
"""
from copy import deepcopy
from pathlib import Path
import unittest

import stable_state_layout as state
from test_stable_state_layout import fixture


ROOT = Path(__file__).resolve().parents[2]
U4L7 = (ROOT / "app/src/main/assets/content/es/programming/"
        "programming_practice_tools/proyecto_assetcheck/"
        "lesson_disenar_probar_y_presentar_variante_propia.json")


U4L3 = (ROOT / "app/src/main/assets/content/es/programming/"
        "programming_practice_tools/proyecto_assetcheck/"
        "lesson_preparar_y_comprobar_cmake_en_windows.json")


class LocaleOriginDiagnosticTest(unittest.TestCase):
    def test_exact_display_limit_does_not_claim_more_differences(self):
        source = state.prepare_v2(fixture(), "es", {"es": fixture()})
        own = source["stateLayout"]["origin"]
        foreign = deepcopy(own)
        foreign[0]["blocks"][0]["sha256"] = "0" * 64
        foreign[1]["blocks"][1]["sha256"] = "1" * 64
        exactly_two = state._describe_locale_origin_difference(
            own, foreign, "es", limit=2)
        self.assertEqual(exactly_two.count("family.es.origin="), 2)
        self.assertNotIn("further differences omitted", exactly_two)
        foreign[2]["blocks"][0]["sha256"] = "2" * 64
        more_than_two = state._describe_locale_origin_difference(
            own, foreign, "es", limit=2)
        self.assertEqual(more_than_two.count("family.es.origin="), 2)
        self.assertIn("further differences omitted", more_than_two)

    def test_actual_u4l3_reports_every_disagreement_until_exact_limit(self):
        if not U4L3.is_file():
            self.skipTest("Published U4L3 is unavailable")
        document = state.load(U4L3)
        own = document["stateLayout"]["origin"]
        foreign = document["stateLayout"]["family"]["es"]["origin"]
        if own == foreign:
            state.validate(document)
            return  # Work may complete the historical correction in the future.
        count = sum(left != right for old, new in zip(own, foreign)
                    for left, right in zip(old["blocks"], new["blocks"]))
        with self.assertRaisesRegex(
                state.LayoutError, "This locale must keep its own historical origin") as caught:
            state.validate(document)
        detail = str(caught.exception)
        displayed = min(8, count)
        self.assertEqual(detail.count("family.es.origin="), displayed)
        self.assertEqual("further differences omitted" in detail, count > 8)
        self.assertIn("p1 b5 block_0000_0004", detail)

    def test_two_changes_in_an_immutable_origin_are_both_identified(self):
        initial = fixture()
        good = state.prepare_v2(initial, "es", {"es": initial})
        state.validate(good)
        changed = deepcopy(good)
        family = changed["stateLayout"]["family"]["es"]["origin"]
        family[0]["blocks"][0]["sha256"] = "0" * 64
        family[1]["blocks"][1]["sha256"] = "1" * 64
        expected = (
            ("p1 b1 block_0000_0000", "family.es.origin=" + "0" * 64),
            ("p2 b2 block_0001_0001", "family.es.origin=" + "1" * 64),
        )
        with self.assertRaisesRegex(
                state.LayoutError, "This locale must keep its own historical origin") as caught:
            state.validate(changed)
        diagnostic = str(caught.exception)
        for location, digest in expected:
            with self.subTest(position=location):
                self.assertIn(location, diagnostic)
                self.assertIn(digest, diagnostic)
        # No mutation, re-baselining, or automatic approval.
        self.assertEqual(state.validate(good),
                         state.validate(state.prepare_v2(initial, "es", {"es": initial})))

    def test_actual_published_u4l7_is_reported_without_forging_a_fix(self):
        if not U4L7.is_file():
            self.skipTest("The actual U4L7 asset is not part of this checkout")
        document = state.load(U4L7)
        own = document["stateLayout"]["origin"]
        family = document["stateLayout"]["family"]["es"]["origin"]
        if own == family:
            # Future Work revision: still require the whole contract to pass,
            # not simply equality of these two JSON fields.
            state.validate(document)
            return

        with self.assertRaisesRegex(
                state.LayoutError, "This locale must keep its own historical origin") as caught:
            state.validate(document)
        detail = str(caught.exception)
        self.assertIn("p6 b3 block_0005_0002", detail)
        self.assertIn("p9 b3 block_0008_0002", detail)
        self.assertIn("origin=c82fd37c3ded99ed44aafb2b757fd07c590def117e3ca8bc04d807fc722213ec",
                      detail)
        self.assertIn("origin=fa0460ffd2c3b2296de310bd6ad697e6d79ae4471a2102f8f4bae7cdaa683174",
                      detail)


if __name__ == "__main__":
    unittest.main(verbosity=2)
