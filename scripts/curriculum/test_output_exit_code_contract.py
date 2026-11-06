"""Intentional normal termination is an explicit per-input output criterion."""

import copy
from functools import lru_cache
import json
import unittest

import stable_state_layout as state_layout
import validar_json_programacion as contract


L13_RELATIVE = ("app/src/main/assets/content/es/programming/cpp_fundamentals/"
                "archivos_y_programa_completo/lesson_compartir_flujos_sin_acoplar_el_calculo.json")
L13_EXPLANATION = (
    "La segunda extracción falla. Los valores candidatos no se publican: se conservan 9 y 4, "
    "se muestra Entrada invalida|9:4 y el programa termina con código 1. Esto no restaura "
    "los caracteres consumidos ni el estado del flujo.")


@lru_cache(maxsize=1)
def l13_family():
    return {locale: json.loads((contract.ROOT / L13_RELATIVE.replace("/es/", f"/{locale}/")).read_text(encoding="utf-8"))
            for locale in ("es", "en", "fr")}


def l13_proposal():
    family = copy.deepcopy(l13_family())
    before = family["es"]
    proposal = copy.deepcopy(before)
    block = state_layout._blocks(proposal)[state_layout.INTENTIONAL_EXIT_GOAL["identity"]]
    if "outputCheck" in block or state_layout.fingerprint(block) != state_layout.INTENTIONAL_EXIT_GOAL["before"]:
        raise AssertionError("The L13 QA proposal requires its unchanged published exploratory editor")
    block["outputCheck"] = {"cases": [{"standardInput": "", "outputs": ["Entrada invalida|9:4"],
                                      "exitCode": 1, "explanation": L13_EXPLANATION}]}
    return before, state_layout.prepare_v2(proposal, "es", family, before)


class OutputExitCodeContractTest(unittest.TestCase):
    def setUp(self):
        self.block = {"type": "cpp_compiler", "language": "cpp",
                      "initialCode": "int main() { return 1; }"}
        self.case = {"standardInput": "", "outputs": ["Entrada invalida|9:4"],
                     "exitCode": 1, "explanation": "Se conservaron ambos destinos."}

    def errors(self, cases, **extras):
        contract.ERRORS.clear()
        block = dict(self.block, outputCheck={"cases": cases}, **extras)
        contract.validate_cpp_compiler(block, "synthetic exit-code fixture")
        return list(contract.ERRORS)

    def test_normal_integer_boundaries_are_optional_and_never_rewrite_the_contract(self):
        for code in (0, 1, 2, 255):
            case = dict(self.case, exitCode=code)
            before = copy.deepcopy(case)
            with self.subTest(exit_code=code):
                self.assertEqual([], self.errors([case]))
                self.assertEqual(before, case)
        omitted = {key: value for key, value in self.case.items() if key != "exitCode"}
        self.assertEqual([], self.errors([omitted]))
        self.assertNotIn("exitCode", omitted)

    def test_boolean_decimal_exponent_and_abnormal_status_values_are_rejected(self):
        for token in ("true", "false", "null", "1.0", "1.5", "1e0", "-1", "256", "259",
                      "3221225477", '"1"', "[]", "{}"):
            with self.subTest(token=token):
                self.assertTrue(self.errors([dict(self.case, exitCode=json.loads(token))]))

    def test_exit_status_cannot_disambiguate_the_same_input_or_target(self):
        self.assertTrue(self.errors([self.case, dict(self.case, exitCode=2)]))
        targeted = dict(self.case, target="first")
        self.assertTrue(self.errors([targeted, dict(targeted, exitCode=2)], cmake={"target": "first"},
                                    projectFiles=[{"path": "main.cpp", "initialCode": "int main(){}"},
                                                  {"path": "CMakeLists.txt", "initialCode": "add_executable(first main.cpp)"}]))
        self.assertEqual([], self.errors([self.case, dict(self.case, standardInput="different", exitCode=2)]))
        contract.ERRORS.clear()
        contract.validate_cpp_output_check_contract({
            "cmake": {"target": "first"}, "outputCheck": {"cases": [
                targeted, dict(targeted, target="second", exitCode=2)]}}, "targets")
        self.assertEqual([], contract.ERRORS)

    def test_stderr_and_exact_stdout_remain_independent_authored_criteria(self):
        case = dict(self.case, standardError="nonBlank")
        before = copy.deepcopy(case)
        self.assertEqual([], self.errors([case]))
        self.assertEqual(before, case)
        self.assertEqual(["Entrada invalida|9:4"], case["outputs"])
        for patch in ({"outputs": []}, {"outputs": [False]}, {"standardError": "ignored"},
                      {"exitCodes": [0, 1]}, {"exitCode": None}):
            with self.subTest(patch=patch):
                self.assertTrue(self.errors([dict(case, **patch)]))

    def test_old_exploratory_editor_can_gain_a_goal_without_changing_its_source(self):
        revised = dict(self.block, outputCheck={"cases": [self.case]})
        previous = copy.deepcopy(revised)
        self.assertTrue(contract.preserves_previous_compiler("synthetic.json", 0, 0, self.block, revised, {}))
        self.assertEqual(previous, revised)
        changed = dict(revised, initialCode="int main() { return 0; }")
        self.assertFalse(contract.preserves_previous_compiler("synthetic.json", 0, 0, self.block, changed, {}))

    def test_existing_goals_cannot_silently_change_or_drop_the_exit_criterion(self):
        original = dict(self.block, outputCheck={"cases": [copy.deepcopy(self.case)]})
        for code in (0, 2, 255, None):
            changed = copy.deepcopy(original)
            case = changed["outputCheck"]["cases"][0]
            if code is None:
                del case["exitCode"]
            else:
                case["exitCode"] = code
            with self.subTest(exit_code=code):
                self.assertFalse(contract.preserves_previous_compiler("synthetic.json", 0, 0, original, changed, {}))
                self.assertNotEqual(state_layout.state_fingerprint(original), state_layout.state_fingerprint(changed))
        zero = copy.deepcopy(original)
        del zero["outputCheck"]["cases"][0]["exitCode"]
        self.assertFalse(contract.preserves_previous_compiler("synthetic.json", 0, 0, zero, original, {}))

    def test_stable_layout_keeps_the_new_criterion_and_protects_the_prepared_goal(self):
        block = dict(self.block, outputCheck={"cases": [copy.deepcopy(self.case)]})
        payload = state_layout.state_payload(block)
        self.assertEqual(1, payload["outputCheck"]["cases"][0]["exitCode"])
        self.assertNotIn("explanation", payload["outputCheck"]["cases"][0])
        document = {"id": "synthetic_exit_contract", "title": "Process result",
                    "compilerPuzzlesRequired": True, "pages": [{"blocks": [block]}]}
        prepared = state_layout.prepare(document)
        self.assertEqual({(0, 0): (0, 0)}, state_layout.validate(prepared))
        self.assertEqual(document["pages"], prepared["pages"])
        changed = copy.deepcopy(prepared)
        changed["pages"][0]["blocks"][0]["outputCheck"]["cases"][0]["exitCode"] = 0
        with self.assertRaises(ValueError):
            state_layout.validate(changed)

    def test_real_l13_can_adopt_the_exact_new_goal_in_memory_without_resetting_history(self):
        before, after = l13_proposal()
        identity = state_layout.INTENTIONAL_EXIT_GOAL["identity"]
        old = state_layout._blocks(before)[identity]
        current = state_layout._blocks(after)[identity]
        self.assertEqual({"type", "initialCode"}, set(old))
        self.assertEqual(old, {key: value for key, value in current.items() if key != "outputCheck"})
        self.assertEqual("Entrada invalida|9:4", current["outputCheck"]["cases"][0]["outputs"][0])
        self.assertTrue(contract.preserves_previous_compiler(L13_RELATIVE, 5, 2, old, current, before))
        self.assertEqual(state_layout.state_fingerprint(old), state_layout.state_fingerprint(current))
        self.assertNotEqual(state_layout.fingerprint(old), state_layout.fingerprint(current))
        for field in ("origin", "family", "current", "additions", "retired", "retirementHistory"):
            self.assertEqual(before["stateLayout"].get(field), after["stateLayout"].get(field), field)
        self.assertEqual(before["stateLayout"]["editorial"], after["stateLayout"]["editorial"][:-1])
        for field in ("referenceAfter", "activityAfter", "compilerPuzzlesRequired"):
            self.assertEqual(before.get(field), after.get(field), field)
        state_layout.validate_transition(before, after)
        state_layout.check_localized_family([after, l13_family()["en"], l13_family()["fr"]], ["es", "en", "fr"])
        contract.ERRORS.clear()
        contract.validate_cpp_compiler(current, "L13 QA proposal")
        contract.validate_lesson_state_contract(after, "L13 QA proposal")
        self.assertEqual([], contract.ERRORS)
        self.assertFalse(contract.preserves_previous_compiler(L13_RELATIVE, 5, 2, current, old, after))
        with self.assertRaises(state_layout.LayoutError):
            state_layout.validate_transition(after, before)

    def test_real_l13_alias_is_closed_to_its_original_source_owner_and_exact_new_contract(self):
        before, good = l13_proposal()
        identity = state_layout.INTENTIONAL_EXIT_GOAL["identity"]
        for field, value in (("exitCode", 0), ("exitCode", 2), ("standardInput", "other"),
                             ("outputs", ["Entrada invalida|9:4\n"]), ("outputs", ["different"]),
                             ("explanation", " ")):
            bad = copy.deepcopy(good)
            state_layout._blocks(bad)[identity]["outputCheck"]["cases"][0][field] = value
            with self.subTest(field=field, value=value), self.assertRaises(state_layout.LayoutError):
                state_layout.validate(bad)
        for change in ("source", "lesson", "locale", "identity", "proof", "unmapped", "v1"):
            bad = copy.deepcopy(good)
            block = state_layout._blocks(bad)[identity]
            if change == "source":
                block["initialCode"] += "\n// changed source"
            elif change == "lesson":
                bad["id"] += "_other"
            elif change == "locale":
                bad["stateLayout"]["locale"] = "fr"
            elif change == "identity":
                ids = bad["stateLayout"]["current"][5]["blocks"]
                ids[1], ids[2] = ids[2], ids[1]
            elif change == "proof":
                bad["stateLayout"]["editorial"].pop()
            elif change == "unmapped":
                bad.pop("stateLayout")
            else:
                bad["stateLayout"] = {key: value for key, value in bad["stateLayout"].items() if key in {"origin", "current"}}
                bad["stateLayout"]["version"] = 1
            with self.subTest(change=change), self.assertRaises(state_layout.LayoutError):
                state_layout.validate(bad)


if __name__ == "__main__":
    unittest.main()
