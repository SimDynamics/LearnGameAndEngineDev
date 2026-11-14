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

    def test_new_owner_uses_generic_root_contract(self):
        # No production lesson or hardcoded source-hash exception is involved.
        source = {"id": "orden_qa_original", "title": "Dos declaraciones independientes",
                  "pages": [{"blocks": [{
                      "type": "order_code",
                      "question": "Ordena las declaraciones y devuelve el resultado.",
                      "fragments": ["int primero{1};", "int segundo{2};",
                                    "return primero + segundo;"],
                      "correctOrder": [0, 1, 2],
                      "explanation": "Las declaraciones no dependen entre sí."
                  }]}]}
        before = state.prepare_v2(source, "es", {"es": source})
        state.validate(before)
        after = deepcopy(before)
        identity = after["stateLayout"]["current"][0]["blocks"][0]
        self.assertNotIn((after["id"], identity), state.LEGACY_ORDER_EQUIVALENCES)
        after["orderEquivalenceGroups"] = [{
            "activity": identity,
            "sourceSha256": state.state_fingerprint(after["pages"][0]["blocks"][0]),
            "groups": [[0, 1]],
        }]
        state.validate(after)
        state.validate_transition(before, after)
        self.assertEqual(before["pages"], after["pages"])
        self.assertEqual(before["stateLayout"], after["stateLayout"])
        misplaced = deepcopy(after)
        misplaced["orderEquivalenceGroups"][0]["groups"] = [[0, 2]]
        with self.assertRaises(state.LayoutError):
            state.validate(misplaced)

    def test_authored_group_survives_later_lexical_source_format(self):
        # A legitimate future editorial formatting pass must not require
        # rewriting an already published root authoring digest or old answers.
        source = {
            "id": "orden_formato_qa", "title": "Declaraciones independientes",
            "pages": [{"blocks": [{
                "type": "order_code", "question": "Ordena las declaraciones.",
                "fragments": ["int primero{1};", "int segundo{2};",
                              "return primero + segundo;"],
                "correctOrder": [0, 1, 2],
                "explanation": "Los valores se declaran antes del resultado.",
            }]}],
        }
        original = state.prepare_v2(source, "es", {"es": source})
        declared = deepcopy(original)
        identity = declared["stateLayout"]["current"][0]["blocks"][0]
        digest = state.state_fingerprint(declared["pages"][0]["blocks"][0])
        declared["orderEquivalenceGroups"] = [{
            "activity": identity, "sourceSha256": digest, "groups": [[0, 1]],
        }]
        state.validate_transition(original, declared)
        formatted_candidate = deepcopy(declared)
        formatted_candidate["pages"][0]["blocks"][0]["fragments"][0] = (
            "int primero {1};"
        )
        formatted = state.prepare_v2(
            formatted_candidate, "es", {"es": declared}, declared)
        state.validate(formatted)
        state.validate_transition(declared, formatted)
        self.assertEqual(formatted["orderEquivalenceGroups"],
                         declared["orderEquivalenceGroups"])
        self.assertEqual(formatted["stateLayout"]["origin"],
                         declared["stateLayout"]["origin"])
        self.assertEqual(formatted["stateLayout"]["family"],
                         declared["stateLayout"]["family"])
        archive = formatted["stateLayout"]["sourceFormatRevisions"]
        self.assertEqual(len(archive), 1)
        self.assertEqual(archive[0]["id"], identity)
        self.assertEqual(archive[0]["block"],
                         declared["pages"][0]["blocks"][0])
        self.assertNotEqual(
            state.state_fingerprint(formatted["pages"][0]["blocks"][0]),
            digest, "The test must actually exercise the historical source path")

        forged = deepcopy(formatted)
        forged["pages"][0]["blocks"][0]["fragments"][0] = (
            "int primero {2};"
        )
        with self.assertRaises(state.LayoutError):
            state.validate(forged)
        no_archive = deepcopy(formatted)
        del no_archive["stateLayout"]["sourceFormatRevisions"]
        with self.assertRaises(state.LayoutError):
            state.validate(no_archive)
        replaced_digest = deepcopy(formatted)
        replaced_digest["orderEquivalenceGroups"][0]["sourceSha256"] = (
            state.state_fingerprint(formatted["pages"][0]["blocks"][0"])
        )
        with self.assertRaises(state.LayoutError):
            state.validate_transition(declared, replaced_digest)

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
