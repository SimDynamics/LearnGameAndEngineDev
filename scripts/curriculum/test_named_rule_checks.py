"""Contrato de autoría general para reglas nombradas, sin tocar los JSON reales."""
from __future__ import annotations

from copy import deepcopy
from pathlib import Path
import json
import unittest
from unittest.mock import patch

import stable_state_layout as state

ROOT = Path(__file__).resolve().parents[2]
LESSON = ROOT / (
    "app/src/main/assets/content/es/programming/programming_practice_tools/"
    "comprobaciones_repetibles/lesson_nombrar_reglas_y_separar_acuerdos.json"
)


def original() -> dict:
    return historical_fixture(state.load(LESSON))


def historical_fixture(current: dict) -> dict:
    """Validate published authoring before deriving the pre-declaration QA model."""
    state.validate(current)
    before = deepcopy(current)
    before.pop("compilerNamedRuleChecks", None)
    state.validate(before)
    return before


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
         "commentOnlyPaths": ["componente/publico/envio/cajas.h"],
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
            lambda v: v["compilerNamedRuleChecks"][1].update(
                commentOnlyPaths=["pruebas/cajas_casos.cpp"]),
            lambda v: v["compilerNamedRuleChecks"][1].update(
                commentOnlyPaths=["no_existe.h"]),
            lambda v: v["compilerNamedRuleChecks"][2].update(
                commentOnlyPaths=["componente/publico/envio/cajas.h"]),
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


def declaration_with_batteries() -> list[dict]:
    checks = declaration()
    checks[1]["executionProbe"]["batteryCheck"] = {
        "cases": [
            {"function": "cajas_para", "input": value, "expected": expected}
            for value, expected in ((0, 0), (4, 1), (8, 2), (20, 4), (1, 1),
                                    (5, 1), (17, 3), (6, 1), (7, 2))
        ] + [
            {"function": "llega_a_tiempo", "input": 4, "expected": 1},
            {"function": "llega_a_tiempo", "input": 5, "expected": 0},
        ]
    }
    for check in checks[2:]:
        check["executionProbe"] = {
            "harness": "", "header": "", "cases": [
                {"function": "precio_total", "input": 11, "expected": 77},
                {"function": "alcanza_minimo", "input": 4, "expected": 0},
                {"function": "alcanza_minimo", "input": 5, "expected": 1},
            ],
            "batteryCheck": {
                "cases": [
                    {"function": name, "input": value, "expected": expected}
                    for value, total, minimum in ((0, 0, 0), (4, 28, 0), (5, 35, 1),
                                                  (6, 42, 1), (10, 70, 1))
                    for name, expected in (("precio_total", total), ("alcanza_minimo", minimum))
                ],
                "failureExitCode": 1,
            },
        }
    return checks


class NamedRuleBatteryAuthoringTest(unittest.TestCase):
    def setUp(self) -> None:
        self.before = original()
        self.after = deepcopy(self.before)
        self.after["compilerNamedRuleChecks"] = declaration_with_batteries()

    def test_batteries_preserve_editors_and_have_their_own_required_cases(self):
        state.validate(self.after)
        state.validate_transition(self.before, self.after)
        for key in ("pages", "stateLayout", "textSemantics"):
            self.assertEqual(self.after[key], self.before[key])
        project = self.after["compilerNamedRuleChecks"][1]["executionProbe"]
        self.assertIn(19, [case["input"] for case in project["cases"]])
        self.assertNotIn(19, [case["input"] for case in project["batteryCheck"]["cases"]])
        self.assertEqual(len(project["batteryCheck"]["cases"]), 11)
        single = self.after["compilerNamedRuleChecks"][2]["executionProbe"]
        self.assertIn(11, [case["input"] for case in single["cases"]])
        self.assertNotIn(11, [case["input"] for case in single["batteryCheck"]["cases"]])
        self.assertEqual(len(single["batteryCheck"]["cases"]), 10)

    def test_absent_battery_keeps_the_existing_declaration_valid(self):
        legacy = deepcopy(self.before)
        legacy["compilerNamedRuleChecks"] = declaration()
        state.validate(legacy)
        state.validate_transition(self.before, legacy)
        self.assertNotIn("batteryCheck", legacy["compilerNamedRuleChecks"][1]["executionProbe"])

    def test_failure_exit_default_and_win32_boundary_are_supported(self):
        state.validate(self.after)  # The project omits failureExitCode; its default is one.
        for code in (1, 2, 0xFFFFFFFF):
            changed = deepcopy(self.after)
            changed["compilerNamedRuleChecks"][1]["executionProbe"]["batteryCheck"]["failureExitCode"] = code
            with self.subTest(code=code):
                state.validate(changed)

    def test_battery_requires_empty_initial_and_first_case_input(self):
        for page, block in ((5, 6), (7, 4), (8, 7)):
            for initial in (True, False):
                changed = deepcopy(self.after)
                owner = changed["pages"][page]["blocks"][block]
                if initial:
                    owner["initialStandardInput"] = "start"
                else:
                    owner["outputCheck"]["cases"][0]["standardInput"] = "start"
                # Exercise this admission guard directly; an immutable-block
                # rejection must not mask an incompatible battery declaration.
                with self.subTest(page=page, initial=initial), self.assertRaises(state.LayoutError):
                    state._validate_compiler_named_rule_checks(changed)
                changed["compilerNamedRuleChecks"] = declaration()
                state._validate_compiler_named_rule_checks(changed)
        optional = deepcopy(self.after)
        owner = optional["pages"][5]["blocks"][6]
        second = deepcopy(owner["outputCheck"]["cases"][0])
        second["standardInput"] = "optional input"
        owner["outputCheck"]["cases"].append(second)
        state._validate_compiler_named_rule_checks(optional)

    def test_battery_rejects_a_graph_for_the_same_owner_only(self):
        changed = deepcopy(self.after)
        changed["compilerCmakeGraphChecks"] = [{
            "activity": "block_0005_0006",
            "targets": [{"name": "comprobar_cajas", "type": "EXECUTABLE"}],
        }]
        with self.assertRaises(state.LayoutError):
            state.validate(changed)
        changed["compilerCmakeGraphChecks"][0]["activity"] = "block_0002_0004"
        state.validate(changed)
        changed["compilerCmakeGraphChecks"][0]["activity"] = "block_0005_0006"
        changed["compilerNamedRuleChecks"] = declaration()
        state.validate(changed)

    def test_first_battery_case_requires_success_from_its_original_target(self):
        for page, block, field, value in ((5, 6, "exitCode", 2),
                                         (7, 4, "exitCode", 1),
                                         (5, 6, "target", "consultar_cajas")):
            changed = deepcopy(self.after)
            first = changed["pages"][page]["blocks"][block]["outputCheck"]["cases"][0]
            first[field] = value
            with self.subTest(page=page, field=field), self.assertRaises(state.LayoutError):
                state._validate_compiler_named_rule_checks(changed)
            changed["compilerNamedRuleChecks"] = declaration()
            state._validate_compiler_named_rule_checks(changed)
        explicit = deepcopy(self.after)
        first = explicit["pages"][5]["blocks"][6]["outputCheck"]["cases"][0]
        first.update(exitCode=0, target="comprobar_cajas")
        state._validate_compiler_named_rule_checks(explicit)
        later = deepcopy(first)
        later.update(standardInput="optional", exitCode=2, target="consultar_cajas")
        explicit["pages"][5]["blocks"][6]["outputCheck"]["cases"].append(later)
        state._validate_compiler_named_rule_checks(explicit)

    def test_battery_shape_cases_and_failure_exit_are_bounded(self):
        invalid = [
            lambda b: b.clear(),
            lambda b: b.update(sourcePath="other.cpp"),
            lambda b: b.update(functions=[]),
            lambda b: b.update(cases=[]),
            lambda b: b.update(cases="cases"),
            lambda b: b.update(cases=[{}]),
            lambda b: b["cases"].append(deepcopy(b["cases"][0])),
            lambda b: b["cases"][0].update(function="missing"),
            lambda b: b["cases"][0].update(function=[]),
            lambda b: b["cases"][0].update(input=True),
            lambda b: b["cases"][0].update(input=2 ** 31),
            lambda b: b["cases"][0].update(expected=-(2 ** 31) - 1),
            lambda b: b["cases"][0].update(expected=True),
            lambda b: b["cases"][-1].update(expected=2),
            lambda b: next(case for case in b["cases"]
                           if case["function"] == "cajas_para" and case["input"] == 6).update(expected=2),
            lambda b: b["cases"][0].update(extra="ignored"),
            lambda b: b.update(cases=b["cases"][:-2]),
            lambda b: b.update(cases=[
                {"function": "cajas_para", "input": n, "expected": n} for n in range(32)
            ] + [{"function": "llega_a_tiempo", "input": 4, "expected": 1}]),
        ]
        invalid += [lambda b, value=value: b.update(failureExitCode=value)
                    for value in (True, False, 0, -1, 1.5, 2 ** 32, "nonzero", None)]
        for index, mutate in enumerate(invalid):
            changed = deepcopy(self.after)
            mutate(changed["compilerNamedRuleChecks"][1]["executionProbe"]["batteryCheck"])
            with self.subTest(case=index), self.assertRaises(state.LayoutError):
                state.validate(changed)
        for value in (None, [], "yes", True):
            changed = deepcopy(self.after)
            changed["compilerNamedRuleChecks"][1]["executionProbe"]["batteryCheck"] = value
            with self.subTest(value=value), self.assertRaises(state.LayoutError):
                state.validate(changed)

    def test_battery_signed_int_and_case_count_boundaries(self):
        changed = deepcopy(self.after)
        battery = changed["compilerNamedRuleChecks"][1]["executionProbe"]["batteryCheck"]
        battery["cases"] = [
            {"function": "cajas_para", "input": value,
             "expected": {6: 1, 7: 2, 19: 4}.get(value, value)}
            for value in [-(2 ** 31), 2 ** 31 - 1, *range(29)]
        ] + [{"function": "llega_a_tiempo", "input": 4, "expected": 1}]
        self.assertEqual(len(battery["cases"]), 32)
        state.validate(changed)

    def test_battery_cannot_be_removed_rewritten_or_moved_after_publication(self):
        for key in ("batteryCheck", "cases", "failureExitCode"):
            changed = deepcopy(self.after)
            probe = changed["compilerNamedRuleChecks"][2]["executionProbe"]
            if key == "batteryCheck":
                probe.pop(key)
            elif key == "cases":
                probe["batteryCheck"][key][0]["expected"] = 1
            else:
                probe["batteryCheck"][key] = 2
            with self.subTest(key=key), self.assertRaises(state.LayoutError):
                state.validate_transition(self.after, changed)
        changed = deepcopy(self.after)
        record = changed["compilerNamedRuleChecks"][1]
        record["batteryCheck"] = record["executionProbe"].pop("batteryCheck")
        with self.assertRaises(state.LayoutError):
            state.validate(changed)

    def test_original_source_must_have_the_instrumentable_named_signatures(self):
        source = self.before["pages"][7]["blocks"][4]["initialCode"]
        cases = self.after["compilerNamedRuleChecks"][2]["executionProbe"]["batteryCheck"]["cases"]
        state._validate_named_rule_battery_source(source, cases)
        state._validate_named_rule_battery_source(
            "int precio_total(int otro_nombre);\nbool alcanza_minimo(int cantidad);\n" + source,
            cases)
        invalid = [
            source.replace("int precio_total(int articulos)", "long precio_total(int articulos)"),
            source.replace("int precio_total(int articulos)", "int precio_total(double articulos)"),
            source.replace("int precio_total(int articulos)", "static int precio_total(int articulos)"),
            source + "\nint precio_total(int articulos) { return 0; }\n",
            source + "\nint precio_total(double articulos);\n",
            "int alcanza_minimo(int otra_cantidad);\n" + source,
            source.replace("return articulos * limite;", "return __LINE__;"),
            source.replace("return articulos * limite;", "std::exit(0); return 0;"),
            "#define precio_total(x) (x)\n" + source,
        ]
        for index, candidate in enumerate(invalid):
            with self.subTest(case=index), self.assertRaises(state.LayoutError):
                state._validate_named_rule_battery_source(candidate, cases)

    def test_checker_requires_main_and_preserves_harmless_comment_text(self):
        driver = self.before["pages"][5]["blocks"][6]["projectFiles"][-1]["initialCode"]
        state._named_rule_battery_driver(driver + '\n// std::exit(0); __FILE__\n')
        for changed in (driver.replace("int main()", "int other()"),
                        driver.replace("int main()", "bool main()"),
                        driver.replace("int main()", "int main() noexcept"),
                        driver.replace("int main() {", "int main() {\n#include <cstddef>"),
                        "#define main other\n" + driver,
                        driver.replace("return 0;", "std::exit(0);"),
                        driver + '\nconst char* marker = "LGAED_NAMED_RULE_BATTERY_END";\n',
                        driver + '\nint lgaed_named_rule_battery_counter = 0;\n'):
            with self.assertRaises(state.LayoutError):
                state._named_rule_battery_driver(changed)


def declaration_with_variations() -> list[dict]:
    checks = declaration_with_batteries()
    checks[0]["executionProbe"]["constantVariations"] = [{
        "function": "cajas_para", "value": 6,
        "cases": [{"function": "cajas_para", "input": value, "expected": expected}
                  for value, expected in ((0, 0), (4, 1), (7, 2), (19, 4))],
    }]
    checks[1]["executionProbe"]["constantVariations"] = [
        {"function": "cajas_para", "value": 5, "cases": [
            {"function": "cajas_para", "input": 5, "expected": 1},
            {"function": "cajas_para", "input": 6, "expected": 2},
            {"function": "cajas_para", "input": 19, "expected": 4},
            {"function": "llega_a_tiempo", "input": 4, "expected": 1},
            {"function": "llega_a_tiempo", "input": 5, "expected": 0},
        ]},
        {"function": "llega_a_tiempo", "value": 3, "cases": [
            {"function": "cajas_para", "input": 6, "expected": 1},
            {"function": "cajas_para", "input": 19, "expected": 4},
            {"function": "llega_a_tiempo", "input": 4, "expected": 0},
            {"function": "llega_a_tiempo", "input": 5, "expected": 0},
        ]},
    ]
    for check in checks[2:]:
        check["executionProbe"]["constantVariations"] = [
            {"function": "precio_total", "value": 8, "cases": [
                {"function": "precio_total", "input": 11, "expected": 88},
                {"function": "alcanza_minimo", "input": 4, "expected": 0},
                {"function": "alcanza_minimo", "input": 5, "expected": 1},
            ]},
            {"function": "alcanza_minimo", "value": 6, "cases": [
                {"function": "precio_total", "input": 11, "expected": 77},
                {"function": "alcanza_minimo", "input": 4, "expected": 0},
                {"function": "alcanza_minimo", "input": 5, "expected": 0},
            ]},
        ]
    return checks


class NamedRuleConstantVariationAuthoringTest(unittest.TestCase):
    def setUp(self) -> None:
        self.before = original()
        self.after = deepcopy(self.before)
        self.after["compilerNamedRuleChecks"] = declaration_with_variations()

    def test_initial_activation_preserves_all_original_content_and_state(self):
        state.validate(self.after)
        state.validate_transition(self.before, self.after)
        for key in ("pages", "stateLayout", "textSemantics"):
            self.assertEqual(self.before[key], self.after[key])
        for record in self.after["compilerNamedRuleChecks"]:
            self.assertIn("constantVariations", record["executionProbe"])

    def test_absence_keeps_legacy_declarations_unchanged(self):
        for records in (declaration(), declaration_with_batteries()):
            current = deepcopy(self.before)
            current["compilerNamedRuleChecks"] = records
            preserved = deepcopy(current)
            state.validate(current)
            state.validate_transition(self.before, current)
            self.assertEqual(preserved, current)
            for record in records:
                self.assertNotIn("constantVariations", record.get("executionProbe", {}))

    def test_published_fixture_is_validated_before_extracting_history(self):
        preserved = deepcopy(self.after)
        with patch.object(state, "load", return_value=self.after):
            self.assertEqual(self.before, original())
        self.assertEqual(preserved, self.after)
        invalid = deepcopy(self.after)
        invalid["compilerNamedRuleChecks"][0]["executionProbe"]["constantVariations"] = []
        with patch.object(state, "load", return_value=invalid), self.assertRaises(state.LayoutError):
            original()

    def test_published_variations_cannot_be_removed_or_rewritten(self):
        mutations = (
            lambda probe: probe.pop("constantVariations"),
            lambda probe: probe["constantVariations"][0].update(value=7),
            lambda probe: probe["constantVariations"][0]["cases"][2].update(expected=3),
        )
        for mutate in mutations:
            changed = deepcopy(self.after)
            mutate(changed["compilerNamedRuleChecks"][0]["executionProbe"])
            state.validate(changed)
            with self.subTest(mutate=mutate), self.assertRaises(state.LayoutError):
                state.validate_transition(self.after, changed)

    def test_numeric_values_accept_integral_json_only_in_the_new_extension(self):
        changed = deepcopy(self.after)
        for record in changed["compilerNamedRuleChecks"]:
            for variation in record["executionProbe"]["constantVariations"]:
                variation["value"] = float(variation["value"])
                for case in variation["cases"]:
                    case["input"], case["expected"] = float(case["input"]), float(case["expected"])
        state.validate(state.parse_json(json.dumps(changed)))
        for value in (-(2 ** 31), 2 ** 31 - 1, float(-(2 ** 31)), float(2 ** 31 - 1)):
            current = deepcopy(self.after)
            variation = current["compilerNamedRuleChecks"][0]["executionProbe"]["constantVariations"][0]
            variation["value"] = value
            variation["cases"].append({"function": "cajas_para", "input": value, "expected": value})
            with self.subTest(value=value):
                state.validate(current)
        for field in ("value", "input", "expected"):
            current = deepcopy(self.after)
            record = current["compilerNamedRuleChecks"][0]
            if field == "value":
                record["rules"][0][field] = 4.0
            else:
                record["executionProbe"]["cases"][0][field] = 0.0
            with self.subTest(legacy=field), self.assertRaises(state.LayoutError):
                state.validate(current)

    def test_numeric_values_reject_booleans_fractions_nonfinite_and_overflow(self):
        invalid = (True, False, None, "6", [], {}, 6.5, float("nan"), float("inf"),
                   -float("inf"), -(2 ** 31) - 1, 2 ** 31, 10 ** 400)
        for field in ("value", "input", "expected"):
            for value in invalid:
                changed = deepcopy(self.after)
                variation = changed["compilerNamedRuleChecks"][0]["executionProbe"]["constantVariations"][0]
                target = variation if field == "value" else variation["cases"][0]
                target[field] = value
                with self.subTest(field=field, value=value), self.assertRaises(state.LayoutError):
                    state.validate(changed)

    def test_alternatives_and_cases_have_bounded_exact_structure(self):
        changes = (
            lambda values: values.clear(),
            lambda values: values.extend(deepcopy(values) * 8),
            lambda values: values.append(deepcopy(values[0])),
            lambda values: values[0].update(value=4),
            lambda values: values[0].update(function="otro"),
            lambda values: values[0].update(function=[]),
            lambda values: values[0].update(source_path="otro.cpp"),
            lambda values: values[0].pop("value"),
            lambda values: values[0].update(cases=[]),
            lambda values: values[0].update(cases=[
                {"function": "cajas_para", "input": i, "expected": i} for i in range(33)]),
            lambda values: values[0]["cases"].append(deepcopy(values[0]["cases"][0])),
            lambda values: values[0]["cases"][0].update(function="otro"),
            lambda values: values[0]["cases"][0].update(function=[]),
            lambda values: values[0]["cases"][0].update(extra=True),
            lambda values: values[0]["cases"][0].pop("expected"),
        )
        for mutate in changes:
            changed = deepcopy(self.after)
            mutate(changed["compilerNamedRuleChecks"][0]["executionProbe"]["constantVariations"])
            with self.subTest(mutate=mutate), self.assertRaises(state.LayoutError):
                state.validate(changed)
        for value in (None, {}, "alternatives"):
            changed = deepcopy(self.after)
            changed["compilerNamedRuleChecks"][0]["executionProbe"]["constantVariations"] = value
            with self.subTest(value=value), self.assertRaises(state.LayoutError):
                state.validate(changed)

    def test_each_variation_changes_a_shared_target_and_preserves_every_other_function(self):
        mutations = (
            lambda variation: variation["cases"][1].update(expected=1),
            lambda variation: variation["cases"][1].update(input=8),
            lambda variation: variation.update(cases=variation["cases"][:3]),
            lambda variation: variation["cases"][3].update(expected=0),
            lambda variation: variation["cases"][4].update(expected=1),
            lambda variation: (variation["cases"][3].update(input=8),
                               variation["cases"][4].update(input=9)),
        )
        for mutate in mutations:
            changed = deepcopy(self.after)
            variation = changed["compilerNamedRuleChecks"][1]["executionProbe"]["constantVariations"][0]
            mutate(variation)
            with self.subTest(mutate=mutate), self.assertRaises(state.LayoutError):
                state.validate(changed)

    def test_new_structure_limits_match_the_portable_validator(self):
        record = self.after["compilerNamedRuleChecks"][0]
        probe = record["executionProbe"]
        validate = state._validate_named_rule_constant_variations
        for path in ("../cajas.cpp", "./cajas.cpp", "/cajas.cpp", "cájas.cpp", "cajas.cc", "a" * 253 + ".cpp"):
            with self.subTest(path=path), self.assertRaises(state.LayoutError):
                validate(path, record["rules"], probe["cases"], probe["constantVariations"])
        for name in ("main", "a" * 129, "lgaed_named_rule_battery_x",
                     "lgaed_boolean_battery_x", "lgaed_integer_battery_x"):
            rules = deepcopy(record["rules"])
            rules[0]["function"] = name
            with self.subTest(name=name), self.assertRaises(state.LayoutError):
                validate(record["path"], rules, probe["cases"], probe["constantVariations"])

    def test_variations_do_not_require_empty_input_but_cannot_share_a_graph_owner(self):
        changed = deepcopy(self.after)
        changed["pages"][2]["blocks"][4]["initialStandardInput"] = "optional input"
        state._validate_compiler_named_rule_checks(changed)
        changed["compilerCmakeGraphChecks"] = [{"activity": "block_0002_0004"}]
        with self.assertRaises(state.LayoutError):
            state._validate_compiler_named_rule_checks(changed)


if __name__ == "__main__":
    unittest.main()
