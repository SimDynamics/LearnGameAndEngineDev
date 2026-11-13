"""Contrato de autoría general para reglas nombradas, sin tocar los JSON reales."""
from __future__ import annotations

from copy import deepcopy
from pathlib import Path
import json
import unittest

import stable_state_layout as state

ROOT = Path(__file__).resolve().parents[2]
LESSON = ROOT / (
    "app/src/main/assets/content/es/programming/programming_practice_tools/"
    "comprobaciones_repetibles/lesson_nombrar_reglas_y_separar_acuerdos.json"
)


def original() -> dict:
    return json.loads(LESSON.read_text(encoding="utf-8"))


def declaration() -> list[dict]:
    return [
        {"activity": "block_0002_0004", "path": "componente/src/cajas.cpp",
         "executionProbe": {"harness": "pruebas/cajas_casos.cpp", "header": "envio/cajas.h",
                            "cases": [
                                {"function": "cajas_para", "input": 0, "expected": 0},
                                {"function": "cajas_para", "input": 4, "expected": 1},
                                {"function": "cajas_para", "input": 19, "expected": 5}
                            ]},
         "rules": [
            {"function": "cajas_para", "value": 4,
             "purposeWords": ["capacidad", "plaza", "cupo"], "minUses": 2}]},
        {"activity": "block_0005_0006", "path": "componente/src/cajas.cpp",
         "editablePaths": ["componente/publico/envio/cajas.h", "pruebas/cajas_casos.cpp"],
         "executionProbe": {"harness": "pruebas/cajas_casos.cpp", "header": "envio/cajas.h",
                            "cases": [
                                {"function": "cajas_para", "input": 6, "expected": 1},
                                {"function": "cajas_para", "input": 7, "expected": 2},
                                {"function": "cajas_para", "input": 19, "expected": 4},
                                {"function": "llega_a_tiempo", "input": 4, "expected": 1},
                                {"function": "llega_a_tiempo", "input": 5, "expected": 0}
                            ]},
         "rules": [
            {"function": "cajas_para", "value": 6,
             "purposeWords": ["capacidad", "plaza", "cupo"], "minUses": 2},
            {"function": "llega_a_tiempo", "value": 4,
             "purposeWords": ["plazo", "dias", "tiempo"], "minUses": 1}]},
        {"activity": "block_0007_0004", "path": "", "rules": [
            {"function": "precio_total", "value": 7,
             "purposeWords": ["precio", "costo", "tarifa"], "minUses": 1},
            {"function": "alcanza_minimo", "value": 5,
             "purposeWords": ["minimo", "cantidad", "umbral"], "minUses": 1}]},
        {"activity": "block_0008_0007", "path": "", "rules": [
            {"function": "precio_total", "value": 7,
             "purposeWords": ["precio", "costo", "tarifa"], "minUses": 1},
            {"function": "alcanza_minimo", "value": 5,
             "purposeWords": ["minimo", "cantidad", "umbral"], "minUses": 1}]},
    ]


class NamedRuleAuthoringTest(unittest.TestCase):
    def setUp(self) -> None:
        self.before = original()
        self.after = deepcopy(self.before)
        self.after["compilerNamedRuleChecks"] = declaration()

    def test_source_roles_attach_without_touching_curriculum(self):
        state.validate(self.before)
        state.validate(self.after)
        state.validate_transition(self.before, self.after)
        self.assertEqual(self.after["pages"], self.before["pages"])
        self.assertEqual(self.after["stateLayout"], self.before["stateLayout"])
        self.assertEqual(self.after["textSemantics"], self.before["textSemantics"])

    def test_source_owner_is_bounded_and_mapped(self):
        invalid = [
            lambda v: v["compilerNamedRuleChecks"].append(
                deepcopy(v["compilerNamedRuleChecks"][0])),
            lambda v: v["compilerNamedRuleChecks"][0].update(activity="block_0000_0000"),
            lambda v: v["compilerNamedRuleChecks"][0].update(path="CMakeLists.txt"),
            lambda v: v["compilerNamedRuleChecks"][2].update(path="main.cpp"),
            lambda v: v["compilerNamedRuleChecks"][0].update(
                editablePaths=["pruebas/cajas_casos.cpp", "pruebas/cajas_casos.cpp"]),
            lambda v: v["compilerNamedRuleChecks"][0].update(
                editablePaths=["../otro_proyecto.cpp"]),
            lambda v: v["compilerNamedRuleChecks"][2].update(
                editablePaths=["main.cpp"]),
            lambda v: v["compilerNamedRuleChecks"][1].pop("executionProbe"),
            lambda v: v["compilerNamedRuleChecks"][0]["executionProbe"].update(
                harness="componente/src/cajas.cpp"),
            lambda v: v["compilerNamedRuleChecks"][0]["executionProbe"].update(
                header="../../outside.h"),
            lambda v: v["compilerNamedRuleChecks"][0]["executionProbe"]["cases"].append(
                deepcopy(v["compilerNamedRuleChecks"][0]["executionProbe"]["cases"][0])),
            lambda v: v["compilerNamedRuleChecks"][1]["executionProbe"].update(
                cases=[case for case in v["compilerNamedRuleChecks"][1]["executionProbe"]["cases"]
                       if case["function"] != "llega_a_tiempo"]),
            lambda v: v["compilerNamedRuleChecks"][0]["rules"][0].update(value=True),
            lambda v: v["compilerNamedRuleChecks"][0]["rules"][0].update(minUses=0),
            lambda v: v["compilerNamedRuleChecks"][0]["rules"][0].update(
                purposeWords=["x"]),
            lambda v: v["compilerNamedRuleChecks"][0]["rules"][0].update(
                function="cajas_para();"),
            lambda v: v["compilerNamedRuleChecks"][1]["rules"].append(
                deepcopy(v["compilerNamedRuleChecks"][1]["rules"][0])),
        ]
        for mutate in invalid:
            changed = deepcopy(self.after)
            mutate(changed)
            with self.subTest(mutate=mutate), self.assertRaises(state.LayoutError):
                state.validate(changed)

    def test_published_source_rule_cannot_be_erased_or_relaxed(self):
        altered = deepcopy(self.after)
        del altered["compilerNamedRuleChecks"]
        with self.assertRaises(state.LayoutError):
            state.validate_transition(self.after, altered)
        altered = deepcopy(self.after)
        altered["compilerNamedRuleChecks"][0]["rules"][0]["minUses"] = 1
        with self.assertRaises(state.LayoutError):
            state.validate_transition(self.after, altered)

    def test_other_locale_cannot_inherit_spanish_source_rules(self):
        forged = deepcopy(self.after)
        forged["stateLayout"]["locale"] = "en"
        with self.assertRaises(state.LayoutError):
            state.validate(forged)


if __name__ == "__main__":
    unittest.main()
