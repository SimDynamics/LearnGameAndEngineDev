"""Source-bound root authoring for independent one-line order cards.

Do not mutate the published Spanish lesson. Work remains the author of any
curricular adoption; this fixture tests metadata and answer preservation.
"""
from copy import deepcopy
from itertools import permutations
import json
from pathlib import Path
import unittest

import stable_state_layout as state

ROOT = Path(__file__).resolve().parents[2]
LESSON = (ROOT / "app/src/main/assets/content/es/programming/cpp_fundamentals/"
          "archivos_y_programa_completo/lesson_extraer_un_tipo_sin_cambiarlo_en_fallo.json")
OWNER = "block_0004_0003"
GROUP = [2, 3, 4]


def original():
    return json.loads(LESSON.read_text(encoding="utf-8"))


def authored():
    before = original()
    after = deepcopy(before)
    block = state._blocks(after)[OWNER]
    after["orderEquivalenceGroups"] = [{
        "activity": OWNER,
        "sourceSha256": state.state_fingerprint(block),
        "groups": [GROUP],
    }]
    return before, after


class IndependentOrderAuthoring(unittest.TestCase):
    def test_original_owner_and_six_permutations_are_declared(self):
        before, after = authored()
        state.validate(before)
        state.validate(after)
        state.validate_transition(before, after)
        block = state._blocks(after)[OWNER]
        self.assertEqual(block["type"], "order_code")
        self.assertEqual(len(list(permutations(GROUP))), 6)
        self.assertEqual(block["correctOrder"], state._blocks(before)[OWNER]["correctOrder"])
        self.assertEqual(block["fragments"], state._blocks(before)[OWNER]["fragments"])
        self.assertEqual(after["stateLayout"], before["stateLayout"])
        self.assertEqual(after["pages"], before["pages"])
        self.assertNotIn("orderEquivalenceGroups", before)
        self.assertEqual(len({state._blocks(before)[OWNER]["fragments"][i]
                              for i in GROUP}), 3)

    def test_wrong_owners_sources_and_nonindependent_groups_are_rejected(self):
        before, valid = authored()
        invalid = [
            lambda d: d["orderEquivalenceGroups"][0].update(activity="missing"),
            lambda d: d["orderEquivalenceGroups"][0].update(sourceSha256="0" * 64),
            lambda d: d["orderEquivalenceGroups"][0].update(sourceSha256="ZZ"),
            lambda d: d["orderEquivalenceGroups"].append(deepcopy(d["orderEquivalenceGroups"][0])),
            lambda d: d["orderEquivalenceGroups"][0].update(groups=[]),
            lambda d: d["orderEquivalenceGroups"][0].update(groups=[[2]]),
            lambda d: d["orderEquivalenceGroups"][0].update(groups=[[2, 2]]),
            lambda d: d["orderEquivalenceGroups"][0].update(groups=[[2, 3], [3, 4]]),
            lambda d: d["orderEquivalenceGroups"][0].update(groups=[[2, 3, 999]]),
            lambda d: d["orderEquivalenceGroups"][0].update(groups=[[0, 2, 3]]),
            lambda d: d["orderEquivalenceGroups"][0].update(groups=[["2", 3]]),
            lambda d: d["orderEquivalenceGroups"][0].update(groups=[[2, True]]),
            lambda d: d["orderEquivalenceGroups"][0].update(groups=[[2, 3, 4]] * 17),
            lambda d: d["orderEquivalenceGroups"][0].update(
                groups=[[2, 3, 4]], unexpected=True),
            lambda d: d["stateLayout"].update(locale="en"),
        ]
        for mutate in invalid:
            after = deepcopy(valid)
            mutate(after)
            with self.subTest(mutation=mutate), self.assertRaises(state.LayoutError):
                state.validate(after)
        # The review cannot rename a physical card or the canonical answer.
        changed = deepcopy(valid)
        block = state._blocks(changed)[OWNER]
        block["fragments"][2] += " // changed"
        with self.assertRaises(state.LayoutError):
            state.validate(changed)

    def test_published_group_is_append_only_and_old_contracts_unchanged(self):
        _, current = authored()
        state.validate(current)
        dropped = deepcopy(current)
        del dropped["orderEquivalenceGroups"]
        with self.assertRaises(state.LayoutError):
            state.validate_transition(current, dropped)
        weakened = deepcopy(current)
        weakened["orderEquivalenceGroups"][0]["groups"] = [[2, 3]]
        with self.assertRaises(state.LayoutError):
            state.validate_transition(current, weakened)
        # A compatible prose-only correction changes neither code source
        # digest nor the named source review.
        reworded = deepcopy(current)
        block = state._blocks(reworded)[OWNER]
        if "question" in block:
            block["question"] += " "
        self.assertEqual(state.state_fingerprint(block),
                         current["orderEquivalenceGroups"][0]["sourceSha256"])


if __name__ == "__main__":
    unittest.main()
