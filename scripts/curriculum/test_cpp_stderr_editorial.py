"""U8L3 keeps its historical proof while compiler guidance remains editorial."""

import copy
from functools import lru_cache
import hashlib
import json
import subprocess
import unittest

import validar_json_programacion as contract
import stable_state_layout as state_layout


RELATIVE = (
    "app/src/main/assets/content/es/programming/cpp_fundamentals/"
    "excepciones_y_recuperacion/lesson_limites_comprobados_y_tipos_de_captura.json"
)
BASELINE = "d1933edf703e19ae0569ae60849b037c48edb687"


@lru_cache(maxsize=1)
def published_lessons():
    previous = json.loads(subprocess.check_output(
        ["git", "show", BASELINE + ":" + RELATIVE], cwd=contract.ROOT))
    current = json.loads((contract.ROOT / RELATIVE).read_text(encoding="utf-8"))
    return previous, current


class CppStderrEditorialTest(unittest.TestCase):
    def setUp(self):
        previous, current = published_lessons()
        self.previous_lesson = copy.deepcopy(previous)
        self.current_lesson = copy.deepcopy(current)
        self.old = state_layout.original_projection(previous)["pages"][2]["blocks"][8]
        self.new = state_layout.original_projection(current)["pages"][2]["blocks"][8]
        self.old = copy.deepcopy(self.old)
        self.new = copy.deepcopy(self.new)

    def preserved(self, old=None, new=None, relative=RELATIVE, pn=2, bn=8):
        return contract.preserves_previous_compiler(
            relative, pn, bn, self.old if old is None else old,
            self.new if new is None else new, self.previous_lesson)

    def test_published_pair_and_runtime_objective_are_exact_and_preserved(self):
        previous = self.old["outputCheck"]["cases"][0]
        current = self.new["outputCheck"]["cases"][0]
        self.assertEqual("nonBlank", current["standardError"])
        self.assertEqual(contract.STDERR_EDITORIAL_REVISION[4:], tuple(
            hashlib.sha256(case["explanation"].encode("utf-8")).hexdigest()
            for case in (previous, current)))
        self.assertEqual({"explanation", "standardError"}, {
            key for key in previous.keys() | current.keys()
            if previous.get(key) != current.get(key)})
        self.assertTrue(self.preserved())
        self.assertEqual(self.old["initialCode"], self.new["initialCode"])
        self.assertEqual(previous["standardInput"], current["standardInput"])
        self.assertEqual(previous["outputs"], current["outputs"])

    def test_explanation_can_follow_an_already_published_stderr_objective(self):
        previous = copy.deepcopy(self.old)
        previous["outputCheck"]["cases"][0]["standardError"] = "nonBlank"
        self.assertTrue(self.preserved(old=previous))

    def test_editorial_guidance_is_not_limited_to_historical_lesson_coordinates(self):
        for relative, pn, bn in (
            (RELATIVE.replace("/es/", "/en/"), 2, 8),
            (RELATIVE.replace("limites_comprobados", "otros_limites"), 2, 8),
            (RELATIVE, 1, 8), (RELATIVE, 2, 7), (RELATIVE, 3, 8),
        ):
            with self.subTest(relative=relative, page=pn, block=bn):
                self.assertTrue(self.preserved(relative=relative, pn=pn, bn=bn))

    def test_source_block_type_and_identity_changes_are_not_licensed(self):
        for key, value in (
            ("initialCode", self.new["initialCode"] + "\n// changed source"),
            ("type", "code"), ("id", "different_editor"),
        ):
            with self.subTest(field=key):
                changed = copy.deepcopy(self.new)
                changed[key] = value
                self.assertFalse(self.preserved(new=changed))

    def test_inputs_stdout_comparison_and_extra_cases_remain_protected(self):
        for field, value in (
            ("standardInput", "different input"), ("outputs", ["different output"]),
        ):
            with self.subTest(field=field):
                changed = copy.deepcopy(self.new)
                changed["outputCheck"]["cases"][0][field] = value
                self.assertFalse(self.preserved(new=changed))
        changed = copy.deepcopy(self.new)
        changed["outputCheck"]["inputComparison"] = "tokens"
        self.assertFalse(self.preserved(new=changed))
        changed = copy.deepcopy(self.new)
        changed["outputCheck"]["cases"].append(copy.deepcopy(changed["outputCheck"]["cases"][0]))
        self.assertFalse(self.preserved(new=changed))

    def test_guidance_can_change_but_an_established_stderr_requirement_cannot_be_removed(self):
        for side in ("before", "after"):
            previous, revised = copy.deepcopy(self.old), copy.deepcopy(self.new)
            case = (previous if side == "before" else revised)["outputCheck"]["cases"][0]
            case["explanation"] += " Unauthorized change."
            with self.subTest(side=side):
                self.assertTrue(self.preserved(old=previous, new=revised))
        changed = copy.deepcopy(self.new)
        changed["outputCheck"]["cases"][0]["reviewFeedback"] = [{"outputs": ["x"], "text": "changed feedback"}]
        self.assertTrue(self.preserved(new=changed))
        self.assertFalse(self.preserved(old=self.new, new=self.old))

    def test_guidance_can_follow_another_existing_case(self):
        previous, revised = copy.deepcopy(self.old), copy.deepcopy(self.new)
        previous_case = copy.deepcopy(previous["outputCheck"]["cases"][0])
        previous_case["standardInput"] = "another case"
        previous["outputCheck"]["cases"].insert(0, previous_case)
        revised["outputCheck"]["cases"].insert(0, copy.deepcopy(previous_case))
        self.assertTrue(self.preserved(old=previous, new=revised))

    def test_guidance_does_not_require_stderr_but_invalid_requirements_are_rejected(self):
        changed = copy.deepcopy(self.new)
        del changed["outputCheck"]["cases"][0]["standardError"]
        self.assertTrue(self.preserved(new=changed))
        for requirement in ("", "nonEmpty", False):
            changed = copy.deepcopy(self.new)
            case = changed["outputCheck"]["cases"][0]
            case["standardError"] = requirement
            with self.subTest(requirement=requirement):
                self.assertFalse(self.preserved(new=changed))

    def test_the_established_stderr_objective_cannot_be_weakened(self):
        previous = copy.deepcopy(self.old)
        previous["outputCheck"]["cases"][0]["standardError"] = "nonBlank"
        changed = copy.deepcopy(self.new)
        del changed["outputCheck"]["cases"][0]["standardError"]
        self.assertFalse(self.preserved(old=previous, new=changed))

    def test_state_layout_preserves_the_draft_address_and_anchors_the_revision(self):
        state_layout.validate_transition(self.previous_lesson, self.current_lesson)
        positions = state_layout.validate(self.current_lesson)
        self.assertEqual((2, 8), positions[(2, 8)])
        self.assertEqual(self.previous_lesson["stateLayout"]["origin"],
                         self.current_lesson["stateLayout"]["origin"])
        self.assertEqual(state_layout.state_fingerprint(self.old),
                         state_layout.state_fingerprint(self.new))
        self.assertNotEqual(state_layout.fingerprint(self.old), state_layout.fingerprint(self.new))


if __name__ == "__main__":
    unittest.main()
