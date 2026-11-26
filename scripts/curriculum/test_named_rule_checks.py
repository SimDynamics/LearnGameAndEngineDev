"""Contrato de autoría general para reglas nombradas, sin tocar los JSON reales."""
from __future__ import annotations

from copy import deepcopy
from pathlib import Path
import json
import unittest
from unittest.mock import patch

import stable_state_layout as state
from published_adoption_fixtures import before_adoption, assert_live_owner_addresses
import compiler_case_revision_fixture_source as case_fixtures
import re

ROOT = Path(__file__).resolve().parents[2]
LESSON = ROOT / (
    "app/src/main/assets/content/es/programming/programming_practice_tools/"
    "comprobaciones_repetibles/lesson_nombrar_reglas_y_separar_acuerdos.json"
)


def original() -> dict:
    return historical_fixture(state.load(LESSON))


def historical_fixture(current: dict) -> dict:
    """Validate live authoring, then read the complete authentic predecessor."""
    state.validate(current)
    before = before_adoption(LESSON, epoch="named-rules-before")
    state.validate(before)
    if "compilerNamedRuleChecks" in before or before["id"] != current["id"]:
        raise ValueError("The pinned named-rule predecessor has the wrong owner or epoch")
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


class PublishedNamedRuleAdoptionTest(unittest.TestCase):
    def test_real_root_adoption_and_subsequent_format_publications(self):
        before = before_adoption(LESSON, epoch="named-rules-before")
        adopted = before_adoption(LESSON, epoch="named-rules-adopted")
        formatted = before_adoption(LESSON)
        self.assertNotIn("compilerNamedRuleChecks", before)
        state.validate_transition(before, adopted)
        state.validate_transition(adopted, formatted)
        self.assertEqual(state._blocks(before), state._blocks(adopted))
        self.assertEqual(adopted["compilerNamedRuleChecks"],
                         formatted["compilerNamedRuleChecks"])
        owners = ("block_0002_0004", "block_0005_0006", "block_0006_0009",
                  "block_0007_0004", "block_0008_0007")
        live = assert_live_owner_addresses(self, LESSON, owners)
        self.assertEqual(live["compilerNamedRuleChecks"],
                         formatted["compilerNamedRuleChecks"])

    def test_separated_named_rules_and_deadline_use_the_published_format_epoch(self):
        # A prospective QA publication, not Work's inaccessible local
        # candidate. Preserve the full aggregate on EOF and every oracle.
        live_bytes = LESSON.read_bytes()
        before = before_adoption(LESSON)
        current = before
        owners = ("block_0007_0004", "block_0008_0007", "block_0006_0009")
        expected_first = ("4\n", "5\n", "1 5\n")
        for identity, first in zip(owners, expected_first):
            old = state._blocks(current)[identity]
            original_case = old["outputCheck"]["cases"][0]
            lines = original_case["outputs"][0].splitlines(keepends=True)
            self.assertEqual(lines[-1], "Bateria: OK\n")
            is_project = "projectFiles" in old
            path = "pruebas/cajas_casos.cpp" if is_project else "main.cpp"
            driver = state._compiler_client_source(old, path)
            self.assertEqual(driver.count("int main() {"), 1)
            branches, cases = [], []
            for line in lines[:-1]:
                if is_project:
                    match = re.fullmatch(
                        r"(piezas|dias)=(-?\d+) esperado=(-?\d+) obtenido=(-?\d+) OK\n", line)
                    self.assertIsNotNone(match)
                    kind, value, expected, actual = match.groups()
                    self.assertEqual(expected, actual)
                    group = 0 if kind == "piezas" else 1
                    function = "comprobar" if group == 0 else "comprobar_plazo"
                    argument = expected if group == 0 else ("true" if expected == "1" else "false")
                    condition = f"tipo == {group} && entrada == {value}"
                    invocation = f"{function}({value}, {argument})"
                    stdin = f"{group} {value}\n"
                else:
                    match = re.fullmatch(
                        r"articulos=(-?\d+) total=(-?\d+) esperado=(-?\d+) "
                        r"minimo=([01]) esperado_minimo=([01]) OK\n", line)
                    self.assertIsNotNone(match)
                    value, total, expected, minimum, expected_minimum = match.groups()
                    self.assertEqual((total, minimum), (expected, expected_minimum))
                    condition = f"entrada == {value}"
                    invocation = f"comprobar({value}, {expected}, " + (
                        "true)" if minimum == "1" else "false)")
                    stdin = value + "\n"
                branches.append((condition, invocation))
                case = {"standardInput": stdin, "outputs": [line + lines[-1]]}
                if "exitCode" in original_case:
                    case["exitCode"] = original_case["exitCode"]
                cases.append(case)
            self.assertEqual(len(cases), 11 if is_project else 5)
            first_index = next(i for i, case in enumerate(cases) if case["standardInput"] == first)
            cases.insert(0, cases.pop(first_index))
            cases.append(deepcopy(original_case))
            branch = "int main() {\n    int entrada{};\n"
            if is_project:
                branch += "    int tipo{};\n    if (std::cin >> tipo >> entrada) {\n"
            else:
                branch += "    if (std::cin >> entrada) {\n"
            branch += "        bool correcto{};\n"
            for index, (condition, invocation) in enumerate(branches):
                branch += "        " + ("if" if index == 0 else "else if")
                branch += f" ({condition}) correcto = {invocation};\n"
            branch += ('        else return 2;\n'
                       '        if (!correcto) { std::cout << "Bateria: FALLO\\n"; return 1; }\n'
                       '        std::cout << "Bateria: OK\\n";\n        return 0;\n    }\n')
            current = state.revise_compiler_client_cases(
                current, identity, {"path": path, "source": driver.replace("int main() {", branch, 1)},
                cases, case_semantics=case_fixtures.authored_fields(current, identity, cases))
            self.assertEqual(state._blocks(current)[identity]["outputCheck"]["cases"][0]["standardInput"],
                             first)
            self.assertEqual(current["compilerNamedRuleChecks"], before["compilerNamedRuleChecks"])
            self.assertEqual(current["stateLayout"]["sourceFormatRevisions"],
                             before["stateLayout"]["sourceFormatRevisions"])
        state.validate_transition(before, current)
        self.assertEqual(state.validate(before), state.validate(current))
        aligned = state.align_for_previous_validation(before, current)
        self.assertEqual(state._blocks(aligned), state._blocks(before))
        for identity in owners:
            record = next(item for item in current["stateLayout"]["compilerCaseRevisions"]
                          if item["id"] == identity)
            self.assertEqual(record["block"], state._blocks(before)[identity])
        forged = deepcopy(current)
        probe = next(item["executionProbe"] for item in forged["compilerNamedRuleChecks"]
                     if item["activity"] == "block_0008_0007")
        probe["batteryCheck"]["cases"].pop()
        with self.assertRaises(state.LayoutError):
            state.validate_transition(before, forged)
        self.assertEqual(LESSON.read_bytes(), live_bytes)


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


class NamedRuleEditableBodiesAuthoringTest(unittest.TestCase):
    def setUp(self) -> None:
        self.before = original()
        self.after = deepcopy(self.before)
        self.after["compilerNamedRuleChecks"] = declaration()
        self.after["compilerNamedRuleChecks"][0]["editableBodies"] = [
            "int cajas_para(int piezas)"]
        self.after["compilerNamedRuleChecks"][2]["editableBodies"] = [
            "int precio_total(int articulos)", "bool alcanza_minimo(int articulos)",
            "int main()"]

    def test_root_body_policy_preserves_the_complete_authentic_publication(self):
        preserved = deepcopy(self.after)
        state.validate(self.after)
        state.validate_transition(self.before, self.after)
        self.assertEqual(preserved, self.after)
        for key in self.before:
            self.assertEqual(self.before[key], self.after[key])
        # main is deliberately not one of rules: the author may authorize
        # another existing body without giving it a named-constant contract.
        record = self.after["compilerNamedRuleChecks"][2]
        self.assertIn("int main()", record["editableBodies"])
        self.assertNotIn("main", {rule["function"] for rule in record["rules"]})

    def test_absence_keeps_all_legacy_declarations_valid_and_unchanged(self):
        for declarations in (declaration(), declaration_with_batteries(),
                             declaration_with_variations()):
            current = deepcopy(self.before)
            current["compilerNamedRuleChecks"] = declarations
            preserved = deepcopy(current)
            state.validate(current)
            state.validate_transition(self.before, current)
            self.assertEqual(preserved, current)
            self.assertTrue(all("editableBodies" not in entry for entry in declarations))

    def test_published_body_policy_cannot_be_added_removed_or_rewritten(self):
        removed = deepcopy(self.after)
        removed["compilerNamedRuleChecks"][0].pop("editableBodies")
        rewritten = deepcopy(self.after)
        rewritten["compilerNamedRuleChecks"][2]["editableBodies"] = ["int main()"]
        legacy = deepcopy(self.before)
        legacy["compilerNamedRuleChecks"] = declaration()
        for before, after in ((self.after, removed), (self.after, rewritten),
                              (legacy, self.after)):
            state.validate(before)
            state.validate(after)
            with self.subTest(after=after["compilerNamedRuleChecks"]), \
                    self.assertRaises(state.LayoutError):
                state.validate_transition(before, after)

    def test_lists_signatures_and_physical_order_are_strict(self):
        signature = "int cajas_para(int piezas)"
        invalid = ([], None, True, 1, {}, signature, (signature,),
                   [None], [True], [7], [[]], [{}], [""], ["int cajas_para"],
                   [signature, signature], [signature] * 9, ["int absent(int value)"],
                   ["int main()"], [signature + "\n"], [signature + "\r"],
                   [signature + "\x00"], ["x" * 513])
        for signatures in invalid:
            changed = deepcopy(self.after)
            changed["compilerNamedRuleChecks"][0]["editableBodies"] = signatures
            with self.subTest(signatures=signatures), self.assertRaises(state.LayoutError):
                state._validate_compiler_named_rule_checks(changed)
        changed = deepcopy(self.after)
        changed["compilerNamedRuleChecks"][2]["editableBodies"].reverse()
        with self.assertRaises(state.LayoutError):
            state.validate(changed)
        # Both strings locate the same body despite being distinct spellings.
        with self.assertRaises(state.LayoutError):
            state._validate_named_rule_editable_bodies(
                signature + " { return 0; }\n", [signature, signature + " "])

    def test_only_real_unique_definitions_bind_bodies(self):
        signature = "int sample(int value)"
        definition = signature + " { return value; }\n"
        decoys = (
            "// " + definition,
            "/* " + definition + " */\n",
            'const char* text = "int sample(int value) { return 0; }";\n',
            'const char* text = R"qa(\n' + definition + ')qa";\n',
            signature + ";\n",
        )
        for decoy in decoys:
            with self.subTest(decoy=decoy), self.assertRaises(state.LayoutError):
                state._validate_named_rule_editable_bodies(decoy, [signature])
            state._validate_named_rule_editable_bodies(decoy + definition, [signature])
        with self.assertRaises(state.LayoutError):
            state._validate_named_rule_editable_bodies(definition + definition, [signature])
        with self.assertRaises(state.LayoutError):
            state._validate_named_rule_editable_bodies(
                "int other() {\n" + definition + "}\n",
                ["int other()", signature])

    def test_signature_cannot_capture_a_statement_or_an_inner_body(self):
        invalid = (
            ("int sample(int value) { if (value)",
             "int sample(int value) { if (value) { return 1; } return 0; }\n"),
            ("int sample(int value) <% if (value)",
             "int sample(int value) <% if (value) <% return 1; %> return 0; %>\n"),
            ("int sample(); int other()", "int sample(); int other() { return 0; }\n"),
            ("int sample(int value) #", "int sample(int value) # { return 0; }\n"),
        )
        for signature, source in invalid:
            with self.subTest(signature=signature), self.assertRaises(state.LayoutError):
                state._validate_named_rule_editable_bodies(source, [signature])
        # Tokens inside comments/literals cannot masquerade as delimiters.
        for signature in ('int sample(int value) /* {}; <% %> # */',
                          'int sample(const char* value = "{;#<%%>}")'):
            state._validate_named_rule_editable_bodies(
                signature + " { return 0; }\n", [signature])

    def test_original_body_uses_the_same_native_lexical_safety_guards(self):
        signature = "int sample(int value)"
        invalid = (
            "#define PRIVATE 1\nreturn value;",
            "%:define PRIVATE 1\nreturn value;",
            "return __LINE__;", "return __builtin_COLUMN();",
            "auto stop = &std::exit; return value;", "std::abort(); return value;",
            "std::raise(0); return value;",
        )
        for body in invalid:
            with self.subTest(body=body), self.assertRaises(state.LayoutError):
                state._validate_named_rule_editable_bodies(
                    signature + " {\n" + body + "\n}\n", [signature])
        for source in (signature + " { return value; }\n\x00",
                       "// continued\\\n" + signature + " { return value; }\n",
                       "// continued\\\r\n" + signature + " { return value; }\n",
                       signature + " { return value;\n",
                       signature + " { return value; }\n/* unclosed"):
            with self.subTest(source=source), self.assertRaises(state.LayoutError):
                state._validate_named_rule_editable_bodies(source, [signature])
        state._validate_named_rule_editable_bodies(
            '/* heading */\n' + signature + ' /* original */ <%\n'
            '// __LINE__, exit and %> are only documentation.\n'
            'const char* label = R"qa(}; # __LINE__ exit(0))qa";\n'
            'const auto unicode_label = u8R"qa(\n{ %> __LINE__ exit(0)\n)qa";\n'
            'const auto simple_label = u8"{};";\n'
            'const int item8{value};\n'
            "const int capacity{0'4}; return value * capacity + 4'0 / 10;\n"
            "%>\n", [signature])

    def test_utf8_byte_source_and_signature_limits_and_eight_body_boundary(self):
        signature = "int sample(int value) /*" + "ñ" * 243 + "*/"
        self.assertEqual(512, len(signature.encode("utf-8")))
        state._validate_named_rule_editable_bodies(
            signature + " { return value; }\n", [signature])
        too_long = signature.replace("*/", "a*/")
        with self.assertRaises(state.LayoutError):
            state._validate_named_rule_editable_bodies(
                too_long + " { return value; }\n", [too_long])
        for source, signatures in (("\ud800", ["int sample()"]),
                                   ("int sample() { return 0; }", ["\ud800()"])):
            with self.subTest(source=repr(source)), self.assertRaises(state.LayoutError):
                state._validate_named_rule_editable_bodies(source, signatures)
        signatures = [f"int body_{index}()" for index in range(8)]
        source = "\n".join(signature + " { return 0; }" for signature in signatures)
        state._validate_named_rule_editable_bodies(source, signatures)
        with self.assertRaises(state.LayoutError):
            state._validate_named_rule_editable_bodies(
                source + "\nint body_8() { return 0; }", signatures + ["int body_8()"])
        tail = "\nint sample() { return 0; }\n"
        source = "//" + "x" * (4 * 1024 * 1024 - 2 - len(tail)) + tail
        state._validate_named_rule_editable_bodies(source, ["int sample()"])
        with self.assertRaises(state.LayoutError):
            state._validate_named_rule_editable_bodies(source + " ", ["int sample()"])


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

    def test_battery_input_modes_preserve_the_complete_private_battery(self):
        for page, block in ((5, 6), (7, 4), (8, 7)):
            for initial in (True, False):
                changed = deepcopy(self.after)
                owner = changed["pages"][page]["blocks"][block]
                if initial:
                    owner["initialStandardInput"] = "start"
                else:
                    owner["outputCheck"]["cases"][0]["standardInput"] = "start"
                # Existing CMake batteries and unrelated initial inputs
                # still reject. Single-file public trials may use stdin.
                with self.subTest(page=page, initial=initial):
                    if page != 5 and not initial:
                        state._validate_compiler_named_rule_checks(changed)
                        owner["initialStandardInput"] = "start"
                        state._validate_compiler_named_rule_checks(changed)
                        owner["initialStandardInput"] = "unrelated"
                    with self.assertRaises(state.LayoutError):
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
