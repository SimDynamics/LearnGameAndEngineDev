"""Author open CMake and boolean battery goals without changing lesson assets."""
from __future__ import annotations

from copy import deepcopy
import json
from pathlib import Path
import unittest

import stable_state_layout as state

ROOT = Path(__file__).resolve().parents[2]
PATH = ROOT / ("app/src/main/assets/content/es/programming/programming_practice_tools/"
               "comprobaciones_repetibles/lesson_integrar_un_proyecto_y_sostener_su_evidencia.json")
OWNER = "block_0006_0011"
CASES = (
    (2, 3, 7, 77), (5, 3, 7, -8), (9, 3, 7, 19),
    (4, 4, 4, -4), (-20, 20, 20, 8), (20, -20, -20, 17),
    (0, 7, 3, 77), (21, 3, 7, -8), (-21, 3, 7, 19),
    (0, -21, 7, -4), (0, 3, 21, 8),
)


def published() -> dict:
    return json.loads(PATH.read_text(encoding="utf-8"))


def authorized(before: dict) -> dict:
    result = deepcopy(before)
    result["compilerOpenBatteryChecks"] = [{
        "activity": OWNER,
        "library": "src/ajuste.cpp",
        "test": "pruebas/casos.cpp",
        "function": "intentar_ajuste",
        "comparison": "comprobar",
        "target": "comprobar_ajustes",
        "originalCases": [
            dict(value=value, lower=lower, upper=upper, initial=initial)
            for value, lower, upper, initial in CASES
        ],
    }]
    return result


BOOLEAN_PATH = ROOT / (
    "app/src/main/assets/content/es/programming/programming_practice_tools/"
    "evidencia_y_diagnostico/lesson_versiones_del_fuente_y_del_ejecutable.json")
BOOLEAN_OWNERS = ("block_0004_0004", "block_0005_0009")
BOOLEAN_THREE = ((4, 5, True), (5, 5, True), (6, 5, False))
BOOLEAN_SIX = (*BOOLEAN_THREE, (7, 8, True), (8, 8, True), (9, 8, False))


def boolean_authorized(before: dict) -> dict:
    result = deepcopy(before)
    result["compilerOpenBatteryChecks"] = [
        {"activity": owner, "kind": "boolean-pair-v1", "function": "debe_reponer",
         "publishedResult": outcome,
         "cases": [dict(first=first, second=second, expected=expected)
                   for first, second, expected in cases]}
        for owner, outcome, cases in zip(
            BOOLEAN_OWNERS, ("failure", "success"), (BOOLEAN_THREE, BOOLEAN_SIX))
    ]
    return result


class OpenBatteryRootContractTest(unittest.TestCase):
    def test_real_root_addition_preserves_all_historical_content(self):
        before = published()
        after = authorized(before)
        self.assertEqual(state.validate(before), state.validate(after))
        state.validate_transition(before, after)
        for name in ("pages", "stateLayout", "activityAfter", "textSemantics"):
            self.assertEqual(before.get(name), after.get(name))
        self.assertNotIn("compilerOpenBatteryChecks", before)
        self.assertEqual(len(after["compilerOpenBatteryChecks"][0]["originalCases"]), 11)

    def test_bad_original_calls_and_forged_semantics_are_rejected(self):
        before = published()
        for label, change in (
            ("missing", lambda r: r["originalCases"].pop()),
            ("swapped", lambda r: r["originalCases"].reverse()),
            ("duplicate", lambda r: r["originalCases"].append(deepcopy(r["originalCases"][0]))),
            ("false_value", lambda r: r["originalCases"][1].update(value=6)),
            ("new_target", lambda r: r.update(target="consultar_ajustes")),
            ("new_library", lambda r: r.update(library="pruebas/casos.cpp")),
            ("other_owner", lambda r: r.update(activity="block_0002_0003")),
            ("bad_comparison", lambda r: r.update(comparison="comprobar;exit(0)")),
            ("escape", lambda r: r.update(test="../private.cpp")),
            ("unknown_field", lambda r: r.update(required=False)),
        ):
            candidate = authorized(before)
            change(candidate["compilerOpenBatteryChecks"][0])
            with self.subTest(bad=label), self.assertRaises(state.LayoutError):
                state.validate(candidate)

    def test_published_declaration_is_immutable(self):
        before = published()
        accepted = authorized(before)
        removed = deepcopy(accepted)
        del removed["compilerOpenBatteryChecks"]
        with self.assertRaises(state.LayoutError):
            state.validate_transition(accepted, removed)
        changed = deepcopy(accepted)
        changed["compilerOpenBatteryChecks"][0]["originalCases"][0]["initial"] = 0
        with self.assertRaises(state.LayoutError):
            state.validate(changed)
        repeated = deepcopy(accepted)
        repeated["compilerOpenBatteryChecks"].append(
            deepcopy(repeated["compilerOpenBatteryChecks"][0]))
        with self.assertRaises(state.LayoutError):
            state.validate(repeated)

    def test_original_asset_is_read_only(self):
        existing = PATH.read_bytes()
        state.validate(authorized(published()))
        self.assertEqual(existing, PATH.read_bytes())


class BooleanBatteryRootContractTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.original_bytes = BOOLEAN_PATH.read_bytes()
        cls.original = json.loads(cls.original_bytes)

    def declared(self):
        return boolean_authorized(self.original)

    def test_real_owners_keep_all_content_and_stable_state_addresses(self):
        after = self.declared()
        self.assertEqual(state.validate(self.original), state.validate(after))
        state.validate_transition(self.original, after)
        self.assertEqual(self.original,
                         {key: value for key, value in after.items()
                          if key != "compilerOpenBatteryChecks"})
        owners = state._blocks(after)
        for identity in BOOLEAN_OWNERS:
            self.assertEqual(state._blocks(self.original)[identity], owners[identity])
        self.assertEqual(after["stateLayout"]["current"][4]["blocks"][4], BOOLEAN_OWNERS[0])
        self.assertEqual(after["stateLayout"]["current"][5]["blocks"][9], BOOLEAN_OWNERS[1])
        self.assertEqual([len(item["cases"]) for item in after["compilerOpenBatteryChecks"]], [3, 6])
        self.assertEqual([item["publishedResult"] for item in after["compilerOpenBatteryChecks"]],
                         ["failure", "success"])
        self.assertEqual(self.original_bytes, BOOLEAN_PATH.read_bytes())

    def test_new_owners_append_but_existing_requirements_stay_literal(self):
        first = self.declared()
        first["compilerOpenBatteryChecks"].pop()
        both = self.declared()
        state.validate_transition(first, both)
        for label, change in (
            ("removed", lambda d: d.pop("compilerOpenBatteryChecks")),
            ("shortened", lambda d: d["compilerOpenBatteryChecks"].pop()),
            ("reordered", lambda d: d["compilerOpenBatteryChecks"].reverse()),
            ("expectation", lambda d: d["compilerOpenBatteryChecks"][0]["cases"][0].update(expected=False)),
            ("outcome", lambda d: d["compilerOpenBatteryChecks"][0].update(publishedResult="success")),
            ("case_order", lambda d: d["compilerOpenBatteryChecks"][0]["cases"].reverse()),
        ):
            changed = deepcopy(both)
            change(changed)
            with self.subTest(change=label), self.assertRaises(state.LayoutError):
                state.validate_transition(both, changed)

    def test_boolean_extension_preserves_an_existing_cmake_declaration(self):
        before = authorized(published())
        plan = deepcopy(before["stateLayout"]["current"])
        identity = "qa_boolean_battery"
        plan[-1]["blocks"].append(identity)
        after = state.reorder(before, plan, [{
            "id": identity, "storage": {"page": 900000, "block": 0},
            "progressPage": plan[-1]["id"],
            "block": {"type": "cpp_compiler", "initialCode":
                      "bool distinct(int left, int right) { return left != right; }\n"
                      "int main() { return 0; }\n"},
        }])
        after["compilerOpenBatteryChecks"].append({
            "activity": identity, "kind": "boolean-pair-v1", "function": "distinct",
            "publishedResult": "success",
            "cases": [{"first": 1, "second": 1, "expected": False},
                      {"first": -2, "second": 3, "expected": True}],
        })
        state.validate_transition(before, after)
        self.assertEqual(before["compilerOpenBatteryChecks"], after["compilerOpenBatteryChecks"][:1])
        self.assertEqual(state._blocks(before)[OWNER], state._blocks(after)[OWNER])
        modified = deepcopy(after)
        modified["compilerOpenBatteryChecks"][0]["originalCases"][0]["initial"] = 0
        with self.assertRaises(state.LayoutError):
            state.validate_transition(after, modified)

    def test_kind_fields_owner_and_result_are_closed(self):
        for label, change in (
            ("unknown_kind", lambda r: r.update(kind="boolean-pair-v2")),
            ("untyped", lambda r: r.pop("kind")),
            ("extra", lambda r: r.update(required=False)),
            ("missing", lambda r: r.pop("function")),
            ("unknown_owner", lambda r: r.update(activity="missing_owner")),
            ("non_editor", lambda r: r.update(activity="block_0000_0000")),
            ("bad_result", lambda r: r.update(publishedResult="nonzero")),
            ("boolean_result", lambda r: r.update(publishedResult=True)),
            ("unsafe_symbol", lambda r: r.update(function="debe_reponer;exit")),
            ("other_symbol", lambda r: r.update(function="another_function")),
        ):
            candidate = self.declared()
            change(candidate["compilerOpenBatteryChecks"][0])
            with self.subTest(bad=label), self.assertRaises(state.LayoutError):
                state.validate(candidate)
        duplicate = self.declared()
        duplicate["compilerOpenBatteryChecks"].append(
            deepcopy(duplicate["compilerOpenBatteryChecks"][0]))
        with self.assertRaisesRegex(state.LayoutError, "Duplicate"):
            state.validate(duplicate)

    def test_input_pairs_and_expectations_are_strictly_typed(self):
        for label, change in (
            ("bool_first", lambda c: c.update(first=True)),
            ("float_second", lambda c: c.update(second=5.0)),
            ("string_first", lambda c: c.update(first="4")),
            ("overflow", lambda c: c.update(first=2 ** 31)),
            ("underflow", lambda c: c.update(second=-(2 ** 31) - 1)),
            ("numeric_true", lambda c: c.update(expected=1)),
            ("numeric_false", lambda c: c.update(expected=0)),
            ("string_true", lambda c: c.update(expected="true")),
            ("missing", lambda c: c.pop("expected")),
            ("extra", lambda c: c.update(label="case")),
        ):
            candidate = self.declared()
            change(candidate["compilerOpenBatteryChecks"][0]["cases"][0])
            with self.subTest(bad=label), self.assertRaisesRegex(state.LayoutError, "signed C\\+\\+ integers"):
                state.validate(candidate)

    def test_case_bounds_distinctness_and_independent_boolean_expectations(self):
        candidate = self.declared()
        record = candidate["compilerOpenBatteryChecks"][0]
        record["cases"] = [{"first": -(2 ** 31), "second": 2 ** 31 - 1, "expected": False}]
        # Authoring records an independent requirement; the validator neither
        # derives expected from the retained function nor assumes <= or a U1 ID.
        state.validate(candidate)
        record["cases"] = [dict(first=index, second=-index, expected=bool(index % 2))
                           for index in range(32)]
        state.validate(candidate)
        for cases in ([], record["cases"] + [dict(first=99, second=99, expected=True)],
                      [dict(first=4, second=5, expected=True),
                       dict(first=4, second=5, expected=False)]):
            changed = deepcopy(candidate)
            changed["compilerOpenBatteryChecks"][0]["cases"] = cases
            with self.subTest(count=len(cases)), self.assertRaises(state.LayoutError):
                state.validate(changed)

    def test_new_goal_cannot_replace_an_existing_objective_or_project(self):
        for field, value in (("outputCheck", {}), ("compileCheck", {}),
                             ("cmake", {}), ("projectFiles", []), ("sourceFiles", []),
                             ("language", "python")):
            candidate = self.declared()
            state._blocks(candidate)[BOOLEAN_OWNERS[0]][field] = value
            with self.subTest(field=field), self.assertRaisesRegex(state.LayoutError, "single-file"):
                state._validate_compiler_open_battery_checks(candidate)
        for field in ("compilerWorkflowChecks", "compilerRuntimeChecks",
                      "compilerNamedRuleChecks", "compilerCmakeGraphChecks"):
            candidate = self.declared()
            candidate[field] = [{"activity": BOOLEAN_OWNERS[0]}]
            with self.subTest(field=field), self.assertRaisesRegex(state.LayoutError, "conflicts"):
                state._validate_compiler_open_battery_checks(candidate)
        for field, value in (("version", 1), ("locale", "en")):
            candidate = self.declared()
            candidate["stateLayout"][field] = value
            with self.subTest(field=field), self.assertRaisesRegex(state.LayoutError, "Spanish v2"):
                state._validate_compiler_open_battery_checks(candidate)

    def test_unique_function_allows_comments_spacing_and_exact_forward_declaration(self):
        definition = "bool distinct(int left, int right) { return left != right; }"
        tokens = state._boolean_battery_definition(definition, "distinct")
        equivalents = (
            "// bool distinct(int left, int right) {}\n" + definition,
            'const char* example = "bool distinct(int left, int right) {";\n' + definition,
            "bool /* note */ distinct ( int left , int right )\n"
            "{ /* braces: {} */ return left != right ; }",
            "#include <iostream>\nbool distinct(int left, int right);\n"
            "int main() { return distinct(1, 2) ? 0 : 1; }\n" + definition,
        )
        for source in equivalents:
            with self.subTest(source=source):
                self.assertEqual(tokens, state._boolean_battery_definition(source, "distinct"))

    def test_missing_ambiguous_or_unsupported_definitions_are_rejected(self):
        definition = "bool distinct(int left, int right) { return left != right; }"
        for source in (
            "int main() { return 0; }", definition + definition,
            definition + "\nbool distinct(int left) { return left != 0; }",
            definition + "\nbool& distinct(int left, int right, int extra) { static bool value; return value; }",
            definition + "\nbool* distinct(int left);",
            "static " + definition, "inline " + definition,
            definition.replace(") {", ") noexcept {"),
            "namespace internal { " + definition + " }",
            "struct Item { " + definition + " };",
            definition.replace("int right", "int& right"),
            definition.replace("int right", "int left"),
            "bool distinct(int a, int b);\n" + definition,
            definition[:-1], definition + "}",
            "#define distinct another\n" + definition,
            "bool dist\\\ninct(int left, int right) { return left != right; }",
            "int lgaed_boolean_battery_hint = 0;\n" + definition,
        ):
            with self.subTest(source=source), self.assertRaises(state.LayoutError):
                state._boolean_battery_definition(source, "distinct")


ADJUSTMENT_OWNER = "block_0002_0003"


def adjustment_authorized(before: dict) -> dict:
    """Add a construction objective; no published block or map is rewritten."""
    result = deepcopy(before)
    result.setdefault("compilerOpenBatteryChecks", []).append({
        "activity": ADJUSTMENT_OWNER, "kind": "cmake-adjustment-project-v1",
        "libraryPath": "src/ajuste.cpp", "headerPath": "publico/control/ajuste.h",
        "header": "control/ajuste.h", "function": "intentar_ajuste",
        "libraryTarget": "ajustes", "clientPath": "app/consulta.cpp",
        "clientTarget": "consultar_ajustes", "testPath": "pruebas/casos.cpp",
        "target": "comprobar_ajustes",
    })
    targets = []
    for name, source, kind in (
            ("ajustes", "src/ajuste.cpp", "STATIC_LIBRARY"),
            ("consultar_ajustes", "app/consulta.cpp", "EXECUTABLE"),
            ("comprobar_ajustes", "pruebas/casos.cpp", "EXECUTABLE")):
        target = dict(name=name, type=kind, sources=[source], exactSources=True,
                      explicitCxx20=True, explicitCxxExtensionsOff=True,
                      requireExplicitMsvcWarnings=True)
        if kind == "STATIC_LIBRARY":
            target["publicIncludeRoots"] = ["publico"]
        else:
            target["linksTo"] = ["ajustes"]
        targets.append(target)
    result.setdefault("compilerCmakeGraphChecks", []).append({
        "activity": ADJUSTMENT_OWNER, "minimumCmakeVersion": "3.20",
        "minimumCmakeVersionAtLeast": True, "targets": targets,
    })
    return result


class AdjustmentProjectRootContractTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.original_bytes = PATH.read_bytes()
        cls.original = json.loads(cls.original_bytes)

    def declared(self):
        return adjustment_authorized(self.original)

    def test_constructor_addition_preserves_sources_maps_input_and_relationships(self):
        after = self.declared()
        self.assertEqual(state.validate(self.original), state.validate(after))
        state.validate_transition(self.original, after)
        self.assertEqual(self.original,
                         {key: value for key, value in after.items()
                          if key not in ("compilerOpenBatteryChecks", "compilerCmakeGraphChecks")})
        owner = state._blocks(after)[ADJUSTMENT_OWNER]
        self.assertEqual(owner, state._blocks(self.original)[ADJUSTMENT_OWNER])
        self.assertEqual(owner["cmake"]["target"], "consultar_ajustes")
        self.assertEqual(owner["initialStandardInput"],
                         "2 3 7\n5 3 7\n9 3 7\n0 7 3\n4 4 4\n21 3 7\n")
        self.assertEqual(next(file["initialCode"] for file in owner["projectFiles"]
                              if file["path"] == "CMakeLists.txt"), "")
        self.assertNotIn("outputCheck", owner)
        self.assertEqual(self.original_bytes, PATH.read_bytes())

    def test_construction_appends_without_weakening_the_existing_extension(self):
        before = authorized(self.original)
        after = adjustment_authorized(before)
        state.validate_transition(before, after)
        self.assertEqual(before["compilerOpenBatteryChecks"], after["compilerOpenBatteryChecks"][:1])
        self.assertEqual(state._blocks(before)[OWNER], state._blocks(after)[OWNER])
        altered = deepcopy(after)
        altered["compilerOpenBatteryChecks"][0]["originalCases"].clear()
        with self.assertRaises(state.LayoutError):
            state.validate(altered)
        duplicate = deepcopy(after)
        duplicate["compilerOpenBatteryChecks"].append(
            deepcopy(duplicate["compilerOpenBatteryChecks"][-1]))
        with self.assertRaisesRegex(state.LayoutError, "Duplicate"):
            state.validate(duplicate)

    def test_constructor_and_graph_declarations_remain_append_only(self):
        before = self.declared()
        for label, change in (
            ("remove_goal", lambda d: d.pop("compilerOpenBatteryChecks")),
            ("remove_graph", lambda d: d.pop("compilerCmakeGraphChecks")),
            ("rewrite_minimum", lambda d: d["compilerCmakeGraphChecks"][0].update(minimumCmakeVersion="3.25")),
            ("rewrite_warning", lambda d: d["compilerCmakeGraphChecks"][0]["targets"][0].update(
                authoredCompileOptions=["/Wall"])),
        ):
            after = deepcopy(before)
            change(after)
            with self.subTest(change=label), self.assertRaises(state.LayoutError):
                state.validate_transition(before, after)

    def test_constructor_fields_and_owners_are_closed(self):
        for field in tuple(self.declared()["compilerOpenBatteryChecks"][0]):
            after = self.declared()
            after["compilerOpenBatteryChecks"][0].pop(field)
            with self.subTest(missing=field), self.assertRaises(state.LayoutError):
                state.validate(after)
        for label, change in (
            ("unsupported_kind", lambda r: r.update(kind="cmake-adjustment-project-v2")),
            ("missing_owner", lambda r: r.update(activity="missing_owner")),
            ("not_editor", lambda r: r.update(activity="block_0000_0000")),
            ("extension_owner", lambda r: r.update(activity=OWNER)),
            ("old_cases", lambda r: r.update(originalCases=[])),
            ("comparison_helper", lambda r: r.update(comparison="comprobar")),
            ("new_input", lambda r: r.update(clientInput="1 2 3")),
            ("optional", lambda r: r.update(required=False)),
        ):
            after = self.declared()
            change(after["compilerOpenBatteryChecks"][0])
            with self.subTest(bad=label), self.assertRaises(state.LayoutError):
                state.validate(after)

    def test_constructor_paths_names_and_original_files_are_authenticated(self):
        for label, change in (
            ("escape", lambda r: r.update(libraryPath="../src/ajuste.cpp")),
            ("empty_component", lambda r: r.update(libraryPath="src//ajuste.cpp")),
            ("backslash", lambda r: r.update(testPath="pruebas\\casos.cpp")),
            ("absolute", lambda r: r.update(clientPath="/app/consulta.cpp")),
            ("wrong_extension", lambda r: r.update(testPath="pruebas/casos.txt")),
            ("duplicate_path", lambda r: r.update(testPath=r["clientPath"])),
            ("foreign_header", lambda r: r.update(header="other/ajuste.h")),
            ("no_public_root", lambda r: r.update(header=r["headerPath"])),
            ("missing_file", lambda r: r.update(clientPath="app/another.cpp")),
            ("unknown_function", lambda r: r.update(function="another_function")),
            ("unsafe_function", lambda r: r.update(function="intentar_ajuste;exit")),
            ("private_function", lambda r: r.update(function="lgaed_adjustment_hint")),
            ("main_function", lambda r: r.update(function="main")),
            ("same_targets", lambda r: r.update(target=r["clientTarget"])),
            ("wrong_initial_target", lambda r: r.update(clientTarget="another_client")),
            ("wrong_type", lambda r: r.update(header=False)),
        ):
            after = self.declared()
            change(after["compilerOpenBatteryChecks"][0])
            with self.subTest(bad=label), self.assertRaises(state.LayoutError):
                state.validate(after)

    def test_shared_graph_is_required_and_cannot_omit_a_construction_obligation(self):
        for label, change in (
            ("minimum_exact", lambda g: g.pop("minimumCmakeVersionAtLeast")),
            ("minimum_false", lambda g: g.update(minimumCmakeVersionAtLeast=False)),
            ("minimum_number", lambda g: g.update(minimumCmakeVersionAtLeast=1)),
            ("minimum_missing", lambda g: g.pop("minimumCmakeVersion")),
            ("minimum_old", lambda g: g.update(minimumCmakeVersion="3.19")),
            ("minimum_range", lambda g: g.update(minimumCmakeVersion="3.20...4.0")),
            ("missing_consumer", lambda g: g["targets"].pop()),
            ("direct_source", lambda g: g["targets"][1]["sources"].append("src/ajuste.cpp")),
            ("inexact_source", lambda g: g["targets"][1].update(exactSources=False)),
            ("no_link", lambda g: g["targets"][2].update(linksTo=[])),
            ("private_header", lambda g: g["targets"][0].pop("publicIncludeRoots")),
            ("extensions", lambda g: g["targets"][2].update(explicitCxxExtensionsOff=False)),
            ("old_cpp", lambda g: g["targets"][0].update(explicitCxx20=False)),
            ("no_warning", lambda g: g["targets"][0].pop("requireExplicitMsvcWarnings")),
            ("numeric_warning", lambda g: g["targets"][1].update(requireExplicitMsvcWarnings=1)),
            ("disabled_warning", lambda g: g["targets"][2].update(requireExplicitMsvcWarnings=False)),
            ("string_warning", lambda g: g["targets"][1].update(requireExplicitMsvcWarnings="/W4")),
        ):
            after = self.declared()
            change(after["compilerCmakeGraphChecks"][0])
            with self.subTest(bad=label), self.assertRaises(state.LayoutError):
                state.validate(after)
        for label, change in (
            ("missing_graph", lambda d: d.pop("compilerCmakeGraphChecks")),
            ("wrong_owner", lambda d: d["compilerCmakeGraphChecks"][0].update(activity=OWNER)),
            ("duplicate_graph", lambda d: d["compilerCmakeGraphChecks"].append(
                deepcopy(d["compilerCmakeGraphChecks"][0]))),
        ):
            after = self.declared()
            change(after)
            with self.subTest(bad=label), self.assertRaises(state.LayoutError):
                state.validate(after)

    def test_minimum_and_warning_levels_accept_equivalent_stronger_requirements(self):
        for minimum in ("3.20", "3.20.0", "3.25", "4.0"):
            for warning in ("/W1", "/W2", "/W3", "/W4", "/Wall"):
                after = self.declared()
                graph = after["compilerCmakeGraphChecks"][0]
                graph["minimumCmakeVersion"] = minimum
                for target in graph["targets"]:
                    target["authoredCompileOptions"] = [warning]
                with self.subTest(minimum=minimum, warning=warning):
                    state.validate(after)

    def test_graph_comparison_and_warning_flags_are_opt_in_and_immutable(self):
        candidate = self.declared()
        candidate.pop("compilerOpenBatteryChecks")
        graph = candidate["compilerCmakeGraphChecks"][0]
        graph["activity"] = "block_0006_0008"
        for value in (False, True):
            graph["minimumCmakeVersionAtLeast"] = value
            for target in graph["targets"]:
                target["requireExplicitMsvcWarnings"] = value
            state.validate(candidate)
            state.validate_transition(self.original, candidate)
        published = deepcopy(candidate)
        graph["minimumCmakeVersionAtLeast"] = False
        with self.assertRaises(state.LayoutError):
            state.validate_transition(published, candidate)
        graph.pop("minimumCmakeVersionAtLeast")
        for target in graph["targets"]:
            target.pop("requireExplicitMsvcWarnings")
        state.validate(candidate)
        for field in ("minimumCmakeVersionAtLeast", "requireExplicitMsvcWarnings"):
            for value in (1, "true", None):
                changed = deepcopy(candidate)
                destination = (changed["compilerCmakeGraphChecks"][0] if field == "minimumCmakeVersionAtLeast"
                               else changed["compilerCmakeGraphChecks"][0]["targets"][0])
                destination[field] = value
                with self.subTest(field=field, value=value), self.assertRaises(state.LayoutError):
                    state.validate(changed)
        missing_minimum = deepcopy(candidate)
        graph = missing_minimum["compilerCmakeGraphChecks"][0]
        graph.pop("minimumCmakeVersion")
        graph["minimumCmakeVersionAtLeast"] = True
        with self.assertRaises(state.LayoutError):
            state.validate(missing_minimum)

    def test_constructor_cannot_replace_other_objectives(self):
        for field, value in (("outputCheck", {}), ("compileCheck", {}),
                             ("sourceFiles", []), ("language", "python")):
            after = self.declared()
            owner = state._blocks(after)[ADJUSTMENT_OWNER]
            owner[field] = value
            with self.subTest(field=field), self.assertRaisesRegex(state.LayoutError, "unevaluated"):
                state._validate_compiler_open_battery_checks(after)
        for field in ("compilerWorkflowChecks", "compilerRuntimeChecks",
                      "compilerNamedRuleChecks", "compilerReductionChecks"):
            after = self.declared()
            after[field] = [{"activity": ADJUSTMENT_OWNER}]
            with self.subTest(field=field), self.assertRaisesRegex(state.LayoutError, "conflicts"):
                state._validate_compiler_open_battery_checks(after)
        legacy = authorized(self.original)
        legacy["compilerCmakeGraphChecks"] = self.declared()["compilerCmakeGraphChecks"]
        legacy["compilerCmakeGraphChecks"][0]["activity"] = OWNER
        with self.assertRaisesRegex(state.LayoutError, "output-check"):
            state._validate_compiler_cmake_graph_checks(legacy)

    def test_header_accepts_formatting_but_never_an_inline_implementation(self):
        valid = (
            "bool intentar_ajuste(int, int, int, int&);",
            "#pragma once\nbool intentar_ajuste ( int value, int low, int high, int & destination );\n",
            "// supplied interface\nbool /* result */ intentar_ajuste(\n"
            "int valor, int inferior, int superior, int& ajuste);\n",
        )
        for source in valid:
            with self.subTest(source=source):
                self.assertTrue(state._adjustment_project_header_valid(source, "intentar_ajuste"))
        for source in (
            "bool intentar_ajuste(int, int, int, int&) { return false; }",
            "inline bool intentar_ajuste(int, int, int, int&);",
            "bool intentar_ajuste(int, int, int, int);",
            "bool intentar_ajuste(int&, int, int, int&);",
            "int intentar_ajuste(int, int, int, int&);",
            "namespace nested { bool intentar_ajuste(int, int, int, int&); }",
            "bool intentar_ajuste(int, int, int, int&);\nint auxiliary;",
            "bool intentar_ajuste(int, int, int, int&);\n"
            "bool intentar_ajuste(int, int, int, int&);",
        ):
            with self.subTest(source=source):
                self.assertFalse(state._adjustment_project_header_valid(source, "intentar_ajuste"))

    def test_client_batch_is_derived_from_the_original_bounded_input(self):
        after = self.declared()
        owner = state._blocks(after)[ADJUSTMENT_OWNER]
        for batch in ("", " \t\r\n", "+2 3 7\n", "1+2-3", "-2147483648 2147483647 0\n",
                      "2 3 7\n" * 8):
            owner["initialStandardInput"] = batch
            with self.subTest(batch=batch):
                state._validate_compiler_open_battery_checks(after)
        for batch in ("1 2", "1 2 3 4", "2 3 7\n" * 9, "2147483648 0 0",
                      "-2147483649 0 0", "1.0 2 3", "x 2 3", "1\u00a02 3",
                      "1\x002 3", "--1 2 3", True):
            owner["initialStandardInput"] = batch
            with self.subTest(batch=batch), self.assertRaises(state.LayoutError):
                state._validate_compiler_open_battery_checks(after)


if __name__ == "__main__":
    unittest.main()

