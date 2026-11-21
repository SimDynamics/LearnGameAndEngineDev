"""Author-only tests of original-owner std::string dependency, without rewriting lessons."""
from copy import deepcopy
import json
from pathlib import Path
import unittest

import stable_state_layout as state


CONTENT = (Path(__file__).resolve().parents[2] /
           "app/src/main/assets/content/es/programming/cpp_fundamentals/"
           "objetos_y_estado/lesson_cadenas_y_texto.json")

EDITION = [
    {
        "activity": "block_0006_0002",
        "probes": [
            {"variable": "edificio", "replacement": "Sala Norte",
             "moment": "afterInitialization",
             "outputs": ["Sala Norte-04", "Sala Norte-04\n"]},
            {"variable": "codigo", "replacement": "003",
             "moment": "afterInitialization",
             "outputs": ["Aula-003", "Aula-003\n"]},
            {"variable": "etiqueta", "replacement": "ETIQUETA QA",
             "moment": "beforeOutput",
             "outputs": ["ETIQUETA QA", "ETIQUETA QA\n"]},
        ],
    },
    {
        "activity": "block_0008_0007",
        "probes": [
            {"variable": "codigo", "replacement": "B2",
             "moment": "afterFirstAssignment",
             "outputs": ["Aula-B2", "Aula-B2\n"]},
            {"variable": "etiqueta", "replacement": "ETIQUETA QA",
             "moment": "beforeOutput",
             "outputs": ["ETIQUETA QA", "ETIQUETA QA\n"]},
        ],
    },
]


def published():
    lesson = json.loads(CONTENT.read_text(encoding="utf-8-sig"))
    state.validate(lesson)
    return lesson


def with_private_checks(document):
    result = deepcopy(document)
    result["compilerLocalStringChecks"] = deepcopy(EDITION)
    return result


class LocalStringAuthoringTest(unittest.TestCase):
    def test_authentic_u2l7_two_owners_preserve_original_cases_and_history(self):
        before = published()
        self.assertNotIn("compilerLocalStringChecks", before)
        after = with_private_checks(before)
        state.validate_transition(before, after)
        state.validate(after)
        self.assertEqual(state._blocks(before), state._blocks(after))
        self.assertEqual(before["stateLayout"], after["stateLayout"])
        self.assertEqual(before["pages"], after["pages"])
        self.assertIn("compilerLocalStringChecks", state.FLOW_FIELDS)
        for id_, expected in (("block_0006_0002", ["Aula-04", "Aula-04\n"]),
                              ("block_0008_0007", ["Aula-07", "Aula-07\n"])):
            current = state._blocks(after)[id_]
            self.assertEqual("cpp_compiler", current["type"])
            self.assertEqual(expected, current["outputCheck"]["cases"][0]["outputs"])
            self.assertNotIn("compilerLocalStringChecks", current)

    def test_authorship_is_append_only_and_no_silent_rewrite(self):
        original = published()
        once = with_private_checks(original)
        extra = deepcopy(once)
        extra["compilerLocalStringChecks"].append({
            "activity": "block_0006_0002",
            "probes": [deepcopy(EDITION[0]["probes"][0])],
        })
        with self.assertRaises(state.LayoutError):
            state.validate(extra)
        for changed in (
            {"compilerLocalStringChecks": []},
            {"compilerLocalStringChecks": deepcopy(EDITION[::-1])},
            {"compilerLocalStringChecks": deepcopy(EDITION[:1])},
        ):
            candidate = deepcopy(once)
            candidate.update(changed)
            with self.subTest(change=changed), self.assertRaises(state.LayoutError):
                state.validate_transition(once, candidate)
        original_case = state._blocks(once)["block_0006_0002"]
        overwritten = deepcopy(once)
        state._blocks(overwritten)["block_0006_0002"]["outputCheck"]["cases"][0]["outputs"] = ["forged"]
        with self.assertRaises(state.LayoutError):
            state.validate_transition(original, overwritten)
        self.assertEqual(["Aula-04", "Aula-04\n"],
                         original_case["outputCheck"]["cases"][0]["outputs"])

    def test_reject_invalid_probe_details_and_unbounded_outputs(self):
        for name, patch in (
            ("wrong-type", {"replacement": 3}),
            ("empty-value", {"replacement": ""}),
            ("nul-value", {"replacement": "A\x00B"}),
            ("too-long-value", {"replacement": "Z" * 129}),
            ("bad-variable", {"variable": "__internal"}),
            ("bad-moment", {"moment": "afterMore"}),
            ("boolean", {"moment": True}),
            ("duplicate-visible", {"outputs": ["Aula-04"]}),
            ("duplicate-newlines", {"outputs": ["Sala Norte-04\n", "Sala Norte-04\r\n"]}),
            ("blank-output", {"outputs": [""]}),
            ("huge-output", {"outputs": ["X" * 4097]}),
            ("extra-key", {"kind": "unpublished"}),
        ):
            doc = with_private_checks(published())
            doc["compilerLocalStringChecks"][0]["probes"][0].update(patch)
            with self.subTest(name=name), self.assertRaises(state.LayoutError):
                state.validate(doc)

    def test_reject_duplicate_owners_unbound_editor_and_crossed_runtime(self):
        for name, mutate in (
            ("repeated-owner", lambda c: c.append(deepcopy(c[0]))),
            ("old-heading", lambda c: c[0].update(activity="block_0008_0000")),
            ("invented-owner", lambda c: c[0].update(activity="block_missing")),
            ("repeated-variable", lambda c: c[0]["probes"].append(deepcopy(c[0]["probes"][0]))),
            ("new-owner-without-cases", lambda c: c[0].update(activity="block_0008_0003")),
        ):
            doc = with_private_checks(published())
            mutate(doc["compilerLocalStringChecks"])
            with self.subTest(name=name), self.assertRaises(state.LayoutError):
                state.validate(doc)
        doc = with_private_checks(published())
        doc["compilerLocalIntChecks"] = [{
            "activity": "block_0006_0002",
            "probes": [{"variable": "edificio", "replacement": 4, "outputs": ["Aula-4"]}],
        }]
        with self.assertRaises(state.LayoutError):
            state.validate(doc)

    def test_unsupported_locales_cannot_claim_original_spanish_owner(self):
        before = published()
        wrong = with_private_checks(before)
        wrong["stateLayout"]["locale"] = "en"
        with self.assertRaises(state.LayoutError):
            state.validate(wrong)


if __name__ == "__main__":
    unittest.main()
