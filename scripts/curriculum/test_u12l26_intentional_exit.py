"""The second deliberate return-1 editor adopts one exact goal, in memory only."""

from copy import deepcopy
from functools import lru_cache
import hashlib
import json
import subprocess
import unittest

import stable_state_layout as layout
import validar_json_programacion as contract


BASELINE = "e8389c86c3cd3b572cb77c6308103163a1e2cf7d"
RELATIVE = ("app/src/main/assets/content/es/programming/cpp_fundamentals/archivos_y_programa_completo/"
            "lesson_construir_un_programa_de_carga_y_guardado.json")
SPEC = next(item for item in layout.INTENTIONAL_EXIT_GOALS if item["identity"] == "block_0004_0017")
EXPLANATION = ("La muestra se crea como un archivo normal. Usarlo como carpeta al abrir la salida falla: "
               "se muestra Error al abrir salida, sin salto final, y el programa termina con código 1.")
PINS = {
    "es": "6b6763a94875c720040322cfe259b7bf1693a743b042ffa30c1a24983e34fd67",
    "en": "97df36676734aa4adf4cfec8573d36ffe1026717988813367df405aee3d87a23",
    "fr": "70cbaaee10733a7740e9b82363d2628d4ab016ef9846925adc09427313b368fd",
}


@lru_cache(maxsize=3)
def published(locale="es"):
    raw = subprocess.check_output(["git", "show", BASELINE + ":" + RELATIVE.replace("/es/", f"/{locale}/")],
                                  cwd=contract.ROOT)
    if hashlib.sha256(raw).hexdigest() != PINS[locale]:
        raise AssertionError("The immutable pre-adoption L26 Git blob differs from its exact byte pin")
    return json.loads(raw)


def editor(document):
    return layout._blocks(document)[SPEC["identity"]]


def proposal():
    family = {locale: deepcopy(published(locale)) for locale in PINS}
    before = family["es"]
    after = deepcopy(before)
    block = editor(after)
    if "outputCheck" in block or layout.fingerprint(block) != SPEC["before"]:
        raise AssertionError("L26 QA requires its exact unchanged exploratory block")
    block["outputCheck"] = {"cases": [{"standardInput": "", "outputs": [SPEC["output"]],
                                      "exitCode": 1, "explanation": EXPLANATION}]}
    return before, layout.prepare_v2(after, "es", family, before)


class U12L26IntentionalExitTest(unittest.TestCase):
    def test_exact_adoption_preserves_every_previous_field_source_identity_and_family(self):
        before, after = proposal()
        self.assertEqual(SPEC["before"], layout.fingerprint(editor(before)))
        self.assertEqual("55e1e30b1c83b9c85cc9de03d6dedec3c310d6b1f2ea098547807aedb348b504",
                         hashlib.sha256(editor(after)["initialCode"].encode()).hexdigest())
        self.assertEqual({"type", "initialCode"}, set(editor(before)))
        self.assertEqual(editor(before), {key: value for key, value in editor(after).items() if key != "outputCheck"})
        self.assertEqual(layout.state_fingerprint(editor(before)), layout.state_fingerprint(editor(after)))
        self.assertNotEqual(layout.fingerprint(editor(before)), layout.fingerprint(editor(after)))
        self.assertEqual(SPEC, layout.reviewed_intentional_exit_spec(editor(after)))
        for field in ("origin", "family", "current", "additions", "retired", "retirementHistory", "orderRefinements"):
            self.assertEqual(before["stateLayout"].get(field), after["stateLayout"].get(field), field)
        self.assertEqual(before["stateLayout"]["editorial"], after["stateLayout"]["editorial"][:-1])
        self.assertEqual({"id": SPEC["identity"], "beforeSha256": SPEC["before"],
                          "afterSha256": layout.fingerprint(editor(after))}, after["stateLayout"]["editorial"][-1])
        restored = deepcopy(after)
        editor(restored).pop("outputCheck")
        restored["stateLayout"]["editorial"].pop()
        self.assertEqual(before, restored)
        self.assertEqual(layout.validate(before), layout.validate(after))
        layout.validate_transition(before, after)
        layout.check_localized_family([after, published("en"), published("fr")], ["es", "en", "fr"])

    def test_curricular_validator_accepts_the_goal_without_modifying_or_weakening_existing_contracts(self):
        before, after = proposal()
        self.assertTrue(contract.preserves_previous_compiler(RELATIVE, 4, 17, editor(before), editor(after), before))
        contract.ERRORS.clear()
        contract.validate_cpp_compiler(editor(after), "U12L26 exact adoption")
        contract.validate_lesson_state_contract(after, "U12L26 exact adoption")
        self.assertEqual([], contract.ERRORS)
        self.assertFalse(contract.preserves_previous_compiler(RELATIVE, 4, 17, editor(after), editor(before), after))
        # Existing zero-exit outputs retain their exact status and raw contents.
        ordinary = {"type": "cpp_compiler", "initialCode": "int main(){}",
                    "outputCheck": {"cases": [{"standardInput": "", "outputs": ["Error al abrir salida"]}]}}
        changed = deepcopy(ordinary)
        changed["outputCheck"]["cases"][0]["exitCode"] = 1
        self.assertFalse(layout.reviewed_intentional_exit_goal(changed))
        self.assertNotEqual(layout.state_fingerprint(ordinary), layout.state_fingerprint(changed))
        self.assertFalse(contract.preserves_previous_compiler("ordinary.json", 0, 0, ordinary, changed, {}))

    def test_every_raw_output_input_status_and_complete_source_guard_is_closed(self):
        _, good = proposal()
        changes = {
            "exit-zero": lambda block, case: case.update(exitCode=0),
            "exit-other": lambda block, case: case.update(exitCode=2),
            "exit-bool": lambda block, case: case.update(exitCode=True),
            "exit-float": lambda block, case: case.update(exitCode=1.0),
            "exit-negative": lambda block, case: case.update(exitCode=-1),
            "exit-abnormal": lambda block, case: case.update(exitCode=3221225477),
            "exit-missing": lambda block, case: case.pop("exitCode"),
            "input": lambda block, case: case.update(standardInput="other"),
            "stdout": lambda block, case: case.update(outputs=["Error al abrir muestra"]),
            "newline": lambda block, case: case.update(outputs=[SPEC["output"] + "\n"]),
            "space": lambda block, case: case.update(outputs=[SPEC["output"] + " "]),
            "alternative": lambda block, case: case["outputs"].append("other"),
            "case": lambda block, case: block["outputCheck"]["cases"].append(deepcopy(case)),
            "comparison": lambda block, case: block["outputCheck"].update(inputComparison="tokens"),
            "stderr": lambda block, case: case.update(standardError="nonBlank"),
            "explanation": lambda block, case: case.update(explanation=" \t"),
            "source": lambda block, case: block.update(initialCode=block["initialCode"] + "\n// altered"),
        }
        for name, change in changes.items():
            bad = deepcopy(good)
            block = editor(bad)
            change(block, block["outputCheck"]["cases"][0])
            bad["stateLayout"]["editorial"][-1]["afterSha256"] = layout.fingerprint(block)
            with self.subTest(change=name), self.assertRaises(layout.LayoutError):
                layout.validate(bad)

    def test_owner_locale_v2_identity_and_real_editorial_edge_are_mandatory(self):
        _, good = proposal()
        for mode in ("owner", "locale", "identity", "missing-edge", "wrong-before", "unmapped", "v1"):
            bad = deepcopy(good)
            if mode == "owner": bad["id"] += "_other"
            if mode == "locale": bad["stateLayout"]["locale"] = "fr"
            if mode == "identity":
                ids = bad["stateLayout"]["current"][4]["blocks"]
                ids[16], ids[17] = ids[17], ids[16]
            if mode == "missing-edge": bad["stateLayout"]["editorial"].pop()
            if mode == "wrong-before": bad["stateLayout"]["editorial"][-1]["beforeSha256"] = "0" * 64
            if mode == "unmapped": bad.pop("stateLayout")
            if mode == "v1":
                bad["stateLayout"] = {key: value for key, value in bad["stateLayout"].items() if key in {"origin", "current"}}
                bad["stateLayout"]["version"] = 1
            with self.subTest(change=mode), self.assertRaises(layout.LayoutError):
                layout.validate(bad)

    def test_published_objective_cannot_be_removed_or_borrow_the_other_exit_alias(self):
        before, after = proposal()
        family = {"es": after, "en": deepcopy(published("en")), "fr": deepcopy(published("fr"))}
        self.assertEqual(after, layout.prepare_v2(deepcopy(after), "es", family, after))
        with self.assertRaises(layout.LayoutError): layout.validate_transition(after, before)
        reverted = deepcopy(after)
        editor(reverted).pop("outputCheck")
        with self.assertRaises(layout.LayoutError): layout.prepare_v2(reverted, "es", family, after)
        borrowed = deepcopy(after)
        editor(borrowed)["outputCheck"]["cases"][0]["outputs"] = [layout.INTENTIONAL_EXIT_GOAL["output"]]
        borrowed["stateLayout"]["editorial"][-1]["afterSha256"] = layout.fingerprint(editor(borrowed))
        with self.assertRaises(layout.LayoutError): layout.validate(borrowed)


if __name__ == "__main__":
    unittest.main()
