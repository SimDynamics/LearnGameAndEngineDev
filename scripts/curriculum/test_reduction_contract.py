"""Authenticate reduction authoring without editing the published U1 lesson."""
from __future__ import annotations

from copy import deepcopy
import json
from pathlib import Path
import unittest

import stable_state_layout as state

ROOT = Path(__file__).resolve().parents[2]
PATH = ROOT / (
    "app/src/main/assets/content/es/programming/programming_practice_tools/"
    "evidencia_y_diagnostico/lesson_reducir_codigo_sin_borrar_el_fallo.json")
OWNER = "block_0002_0003"


def reduction_record(owner: str = OWNER) -> dict:
    return {
        "activity": owner,
        "kind": "stateful-vector-int-v1",
        "function": "contar_altos",
        "state": "acumulado",
        "initialState": 0,
        "calls": [
            {"values": [2], "argument": 2, "result": 1, "stateAfter": 1},
            {"values": [], "argument": 2, "result": 1, "stateAfter": 1},
        ],
        "observedCall": 1,
        "requiredResult": 0,
        "exitCode": 0,
        "dependencies": [
            {"header": "vector", "symbols": ["std::vector"]},
            {"header": "iostream", "symbols": ["std::cout", "std::cerr", "std::clog"]},
            {"header": "string", "symbols": ["std::string", "std::to_string"]},
            {"header": "cstdio", "symbols": ["std::printf", "std::puts"]},
        ],
    }


def authorized(before: dict) -> dict:
    after = deepcopy(before)
    checks = after.setdefault("compilerReductionChecks", [])
    if type(checks) is not list:
        raise AssertionError("Published compilerReductionChecks must be an array")
    existing = [item for item in checks if type(item) is dict and item.get("activity") == OWNER]
    if len(existing) > 1:
        raise AssertionError("The published reduction owner is duplicated")
    if not existing:
        checks.append(reduction_record())
    return after


def owned_record(document: dict, owner: str = OWNER) -> dict:
    matches = [item for item in document["compilerReductionChecks"]
               if type(item) is dict and item.get("activity") == owner]
    if len(matches) != 1:
        raise AssertionError("The reduction fixture must have one requested owner")
    return matches[0]


def different_integer(value: int, *others: int) -> int:
    return next(candidate for candidate in (0, 1, -1, 2)
                if candidate not in (value, *others))


def change_observed_fact(document: dict, field: str) -> None:
    record = owned_record(document)
    call = record["calls"][record["observedCall"]]
    call[field] = different_integer(call[field], record["requiredResult"])


class ReductionRootContractTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.original_bytes = PATH.read_bytes()
        cls.original = json.loads(cls.original_bytes)

    def declared(self):
        return authorized(self.original)

    def source_contract(self, source: str, initial: int = 0,
                        dependencies: list[dict] | None = None):
        return state._reduction_source_contract(
            source, "contar_altos", "acumulado", initial,
            reduction_record()["dependencies"] if dependencies is None else dependencies)

    def test_real_owner_keeps_every_historical_field_and_address(self):
        for label, before in (("published", self.original),
                              ("already_integrated", self.declared())):
            with self.subTest(publication=label):
                after = authorized(before)
                self.assertEqual(state.validate(before), state.validate(after))
                state.validate_transition(before, after)
                self.assertEqual({key: value for key, value in before.items()
                                  if key != "compilerReductionChecks"},
                                 {key: value for key, value in after.items()
                                  if key != "compilerReductionChecks"})
                self.assertEqual(state._blocks(before)[OWNER], state._blocks(after)[OWNER])
                self.assertEqual(after["stateLayout"]["current"][2]["blocks"][3], OWNER)
                self.assertEqual(before["stateLayout"], after["stateLayout"])
                self.assertEqual(after, authorized(after))
        self.assertEqual(self.original_bytes, PATH.read_bytes())

    def test_new_owners_append_without_rewriting_published_goals(self):
        before = self.declared()
        plan = deepcopy(before["stateLayout"]["current"])
        identity = "qa_second_reduction"
        plan[-1]["blocks"].append(identity)
        after = state.reorder(before, plan, [{
            "id": identity, "storage": {"page": 900000, "block": 0},
            "progressPage": plan[-1]["id"],
            "block": deepcopy(state._blocks(before)[OWNER]),
        }])
        after["compilerReductionChecks"].append(reduction_record(identity))
        state.validate_transition(before, after)
        prefix_size = len(before["compilerReductionChecks"])
        self.assertEqual(before["compilerReductionChecks"],
                         after["compilerReductionChecks"][:prefix_size])
        self.assertEqual(state._blocks(before)[OWNER], state._blocks(after)[OWNER])
        # Model a published table where another valid owner precedes this
        # one, and a pre-integration table containing only those other goals.
        existing = deepcopy(after)
        existing["compilerReductionChecks"].reverse()
        self.assertEqual(existing, authorized(existing))
        state.validate_transition(existing, authorized(existing))
        unintegrated = deepcopy(existing)
        unintegrated["compilerReductionChecks"] = [
            item for item in unintegrated["compilerReductionChecks"] if item["activity"] != OWNER]
        appended = authorized(unintegrated)
        self.assertEqual(unintegrated["compilerReductionChecks"],
                         appended["compilerReductionChecks"][:-1])
        state.validate_transition(unintegrated, appended)
        # Equivalent facts may be published with a different dependency
        # policy, including optional facilities absent from the initial code.
        alternate = deepcopy(existing)
        owned_record(alternate)["dependencies"] = [
            *reversed(reduction_record()["dependencies"]),
            {"header": "cctype", "symbols": ["std::isdigit"]},
        ]
        state.validate(alternate)
        self.assertEqual(alternate, authorized(alternate))
        state.validate_transition(alternate, authorized(alternate))
        changes = [
            ("removed", lambda d: d.pop("compilerReductionChecks")),
            ("shortened", lambda d: d["compilerReductionChecks"].pop()),
            ("reordered", lambda d: d["compilerReductionChecks"].reverse()),
            ("result", lambda d: change_observed_fact(d, "result")),
            ("state_after", lambda d: change_observed_fact(d, "stateAfter")),
            ("required", lambda d: owned_record(d).update(requiredResult=different_integer(
                owned_record(d)["requiredResult"],
                owned_record(d)["calls"][owned_record(d)["observedCall"]]["result"]))),
            ("dependency", lambda d: owned_record(d)["dependencies"].pop()),
        ]
        if owned_record(after)["calls"] != list(reversed(owned_record(after)["calls"])):
            changes.append(("call_order", lambda d: owned_record(d)["calls"].reverse()))
        for label, change in changes:
            changed = deepcopy(after)
            change(changed)
            with self.subTest(change=label), self.assertRaises(state.LayoutError):
                state.validate_transition(after, changed)

    def test_disjoint_boolean_and_reduction_owners_coexist(self):
        before = self.declared()
        plan = deepcopy(before["stateLayout"]["current"])
        identity = "qa_boolean_neighbor"
        plan[-1]["blocks"].append(identity)
        after = state.reorder(before, plan, [{
            "id": identity, "storage": {"page": 900000, "block": 0},
            "progressPage": plan[-1]["id"],
            "block": {"type": "cpp_compiler", "initialCode":
                      "bool distinct(int a, int b) { return a != b; }\n"
                      "int main() { return 0; }\n"},
        }])
        after.setdefault("compilerOpenBatteryChecks", []).append({
            "activity": identity, "kind": "boolean-pair-v1", "function": "distinct",
            "publishedResult": "success",
            "cases": [{"first": 1, "second": 2, "expected": True}],
        })
        state.validate_transition(before, after)
        self.assertEqual(before["compilerReductionChecks"], after["compilerReductionChecks"])

    def test_owner_shape_and_kind_are_closed(self):
        for label, change in (
            ("extra", lambda r: r.update(required=False)),
            ("missing", lambda r: r.pop("state")),
            ("wrong_kind", lambda r: r.update(kind="stateful-vector-int-v2")),
            ("missing_owner", lambda r: r.update(activity="missing_owner")),
            ("not_editor", lambda r: r.update(activity="block_0000_0000")),
            ("function", lambda r: r.update(function="contar_altos;exit")),
            ("main", lambda r: r.update(function="main")),
            ("state", lambda r: r.update(state="acumulado;exit")),
            ("same_name", lambda r: r.update(state="contar_altos")),
            ("other_function", lambda r: r.update(function="another_function")),
            ("other_state", lambda r: r.update(state="another_state")),
        ):
            candidate = self.declared()
            change(owned_record(candidate))
            with self.subTest(bad=label), self.assertRaises(state.LayoutError):
                state.validate(candidate)
        for checks in ([], {}, [None], [reduction_record()] * 33):
            candidate = self.declared()
            candidate["compilerReductionChecks"] = checks
            with self.subTest(checks=type(checks).__name__, count=len(checks)), \
                    self.assertRaises(state.LayoutError):
                state.validate(candidate)
        duplicate = self.declared()
        duplicate["compilerReductionChecks"].append(deepcopy(owned_record(duplicate)))
        with self.assertRaisesRegex(state.LayoutError, "Duplicate"):
            state.validate(duplicate)

    def test_new_goal_rejects_unmapped_owners_conflicts_and_projects(self):
        for field, value in (("outputCheck", {}), ("compileCheck", {}),
                             ("cmake", {}), ("projectFiles", []), ("sourceFiles", []),
                             ("language", ""), ("language", "python")):
            candidate = self.declared()
            state._blocks(candidate)[OWNER][field] = value
            with self.subTest(field=field, value=value), self.assertRaisesRegex(state.LayoutError, "single-file"):
                state._validate_compiler_reduction_checks(candidate)
        absent_language = self.declared()
        state._blocks(absent_language)[OWNER].pop("language")
        state._validate_compiler_reduction_checks(absent_language)
        for field in ("compilerWorkflowChecks", "compilerRuntimeChecks",
                      "compilerNamedRuleChecks", "compilerOpenBatteryChecks",
                      "compilerCmakeGraphChecks"):
            candidate = self.declared()
            candidate[field] = [{"activity": OWNER}]
            with self.subTest(conflict=field), self.assertRaisesRegex(state.LayoutError, "conflicts"):
                state._validate_compiler_reduction_checks(candidate)
        boolean_conflict = self.declared()
        boolean_conflict["compilerOpenBatteryChecks"] = [{
            "activity": OWNER, "kind": "boolean-pair-v1", "function": "contar_altos",
            "publishedResult": "success",
            "cases": [{"first": 1, "second": 2, "expected": True}],
        }]
        with self.assertRaisesRegex(state.LayoutError, "conflicts"):
            state._validate_compiler_open_battery_checks(boolean_conflict)
        for metadata in (None, {}, {"version": 1, "locale": "es"},
                         {"version": 2, "locale": "en"}):
            candidate = self.declared()
            candidate["stateLayout"] = metadata
            with self.subTest(metadata=metadata), self.assertRaisesRegex(state.LayoutError, "Spanish v2"):
                state._validate_compiler_reduction_checks(candidate)
        without_map = self.declared()
        without_map.pop("stateLayout")
        with self.assertRaises(state.LayoutError):
            state.validate(without_map)

    def test_integer_fields_do_not_coerce_boolean_float_or_text(self):
        for field in ("initialState", "requiredResult", "exitCode", "observedCall"):
            for value in (True, False, 0.0, "0", None):
                candidate = self.declared()
                owned_record(candidate)[field] = value
                with self.subTest(field=field, value=value), self.assertRaises(state.LayoutError):
                    state.validate(candidate)
        for field in ("initialState", "requiredResult"):
            for value in (-(2 ** 31) - 1, 2 ** 31):
                candidate = self.declared()
                owned_record(candidate)[field] = value
                with self.subTest(field=field, value=value), self.assertRaises(state.LayoutError):
                    state.validate(candidate)
        for field in ("argument", "result", "stateAfter"):
            for value in (True, False, 2.0, "2", -(2 ** 31) - 1, 2 ** 31):
                candidate = self.declared()
                owned_record(candidate)["calls"][0][field] = value
                with self.subTest(field=field, value=value), self.assertRaises(state.LayoutError):
                    state.validate(candidate)
        for value in (True, False, 2.0, "2", -(2 ** 31) - 1, 2 ** 31):
            candidate = self.declared()
            owned_record(candidate)["calls"][0]["values"] = [value]
            with self.subTest(vector_value=value), self.assertRaises(state.LayoutError):
                state.validate(candidate)

    def test_ordered_observations_have_bounded_declared_data(self):
        for label, change in (
            ("one_call", lambda r: r.update(calls=r["calls"][:1])),
            ("no_calls", lambda r: r.update(calls=[])),
            ("too_many_calls", lambda r: r.update(calls=r["calls"] * 17)),
            ("unknown_call_field", lambda r: r["calls"][0].update(expected=1)),
            ("missing_result", lambda r: r["calls"][0].pop("result")),
            ("object_values", lambda r: r["calls"][0].update(values={})),
            ("large_vector", lambda r: r["calls"][0].update(values=[2] * 65)),
            ("unprepared", lambda r: r.update(observedCall=0)),
            ("past_end", lambda r: r.update(observedCall=len(r["calls"]))),
            ("no_contradiction", lambda r: r.update(requiredResult=r["calls"][r["observedCall"]]["result"])),
            ("nonzero_exit", lambda r: r.update(exitCode=1)),
            ("source_initial_mismatch", lambda r: r.update(initialState=different_integer(r["initialState"]))),
        ):
            candidate = self.declared()
            change(owned_record(candidate))
            with self.subTest(bad=label), self.assertRaises(state.LayoutError):
                state.validate(candidate)
        # These are author-declared observations, not values inferred from a
        # known exercise or a built-in counting formula. Repeated inputs can
        # have different results because the function carries state.
        candidate = self.declared()
        record = owned_record(candidate)
        record["calls"] = [
            {"values": [-(2 ** 31), 2 ** 31 - 1] * 32,
             "argument": -(2 ** 31), "result": index, "stateAfter": index}
            for index in range(32)
        ]
        record.update(observedCall=31, requiredResult=2 ** 31 - 1)
        state.validate(candidate)

    def test_dependencies_are_bounded_explicit_and_safe(self):
        for label, dependencies in (
            ("empty", []),
            ("too_many", [{"header": "vector", "symbols": ["std::vector"]}] * 33),
            ("unknown_field", [{"header": "vector", "symbols": ["std::vector"], "optional": True}]),
            ("path", [{"header": "../vector", "symbols": ["std::vector"]}]),
            ("slash", [{"header": "bits/vector.h", "symbols": ["std::vector"]}]),
            ("backslash", [{"header": "bits\\vector", "symbols": ["std::vector"]}]),
            ("delimiter", [{"header": "<vector>", "symbols": ["std::vector"]}]),
            ("repeated_header", [{"header": "vector", "symbols": ["std::vector"]}] * 2),
            ("missing_symbol", [{"header": "vector", "symbols": []}]),
            ("too_many_symbols", [{"header": "vector", "symbols": ["std::vector"] * 33}]),
            ("unqualified", [{"header": "vector", "symbols": ["vector"]}]),
            ("injection", [{"header": "vector", "symbols": ["std::vector;exit"]}]),
            ("empty_part", [{"header": "vector", "symbols": ["std::::vector"]}]),
            ("duplicate_symbol", [{"header": "vector", "symbols": ["std::vector", "std::vector"]}]),
            ("private_name", [{"header": "vector", "symbols": ["std::lgaed_reduction_frame"]}]),
        ):
            candidate = self.declared()
            owned_record(candidate)["dependencies"] = dependencies
            with self.subTest(bad=label), self.assertRaises(state.LayoutError):
                state.validate(candidate)
        candidate = self.declared()
        owned_record(candidate)["dependencies"] = [
            *reduction_record()["dependencies"],
            {"header": "stdio.h", "symbols": ["std::fwrite"]},
        ]
        state.validate(candidate)  # An unused alternative is allowed.

    def test_direct_headers_follow_uses_in_uninvoked_code_and_using_forms(self):
        source = state._blocks(self.original)[OWNER]["initialCode"]
        self.source_contract(source)  # cstdio is declared but unused.
        missing_string = source.replace("#include <string>\n", "")
        with self.assertRaisesRegex(state.LayoutError, "directly included header: string"):
            self.source_contract(missing_string)
        with self.assertRaisesRegex(state.LayoutError, "directly included header: string"):
            self.source_contract(missing_string.replace("const std::string", "using namespace std;\n    const string"))
        with self.assertRaisesRegex(state.LayoutError, "directly included header: string"):
            self.source_contract(missing_string.replace("const std::string", "using std::string;\n    const string"))
        with self.assertRaisesRegex(state.LayoutError, "directly included header: iostream"):
            self.source_contract(source.replace("#include <iostream>\n", ""))
        # Comments and string literals neither create a service use nor
        # manufacture the direct include needed by remaining real code.
        with self.assertRaisesRegex(state.LayoutError, "directly included header: string"):
            self.source_contract("// #include <string>\n" + missing_string)
        reference = self.original["pages"][3]["blocks"][2]["code"]
        self.source_contract(reference + '\nconst char* note = "std::string";\n// std::string\n')

    def test_global_state_admits_equivalent_scalar_initialization(self):
        source = state._blocks(self.original)[OWNER]["initialCode"]
        expected = self.source_contract(source)
        for declaration in ("int acumulado;", "int acumulado{};", "int acumulado = 0;",
                            "int acumulado(0);", "int acumulado{+0};", "int acumulado = -0;"):
            with self.subTest(declaration=declaration):
                self.assertEqual(expected, self.source_contract(
                    source.replace("int acumulado{0};", declaration)))
        for value in (-(2 ** 31), -7, 9, 2 ** 31 - 1):
            self.source_contract(source.replace("int acumulado{0};", f"int acumulado{{{value}}};"), value)

    def test_unique_function_allows_comments_spacing_and_matching_prototype(self):
        source = state._blocks(self.original)[OWNER]["initialCode"]
        expected = self.source_contract(source)
        signature = "int contar_altos(std::vector<int> datos, int minimo)"
        for modified in (
            source.replace(signature, "int /* original */ contar_altos ( std::vector < int > datos , int minimo )"),
            source.replace(signature, signature + ";\n" + signature),
            "// int contar_altos(std::vector<int> datos, int minimo) {}\n" + source,
        ):
            with self.subTest(source=modified):
                self.assertEqual(expected, self.source_contract(modified))

    def test_ambiguous_or_unsupported_source_is_rejected(self):
        source = state._blocks(self.original)[OWNER]["initialCode"]
        signature = "int contar_altos(std::vector<int> datos, int minimo)"
        variants = (
            source + "\n" + source[source.index(signature):source.index("void mostrar_detalle")],
            source + "\nint contar_altos(int x) { return x; }\n",
            source + "\nint& contar_altos(int x) { return acumulado; }\n",
            source + "\nint* contar_altos(int x);\n",
            source.replace(signature, "static " + signature),
            source.replace(signature, "inline " + signature),
            source.replace(signature + " {", signature + " noexcept {"),
            source.replace("std::vector<int> datos, int minimo", "std::vector<int>& datos, int minimo"),
            source.replace("std::vector<int> datos, int minimo", "std::vector<int> datos, int datos"),
            source.replace("std::vector<int> datos, int minimo", "std::vector<int> datos, int acumulado"),
            source.replace(signature, "int contar_altos(std::vector<int> a, int b);\n" + signature),
            source.replace("int acumulado{0};", "static int acumulado{0};"),
            source.replace("int acumulado{0};", "int acumulado{0};\nint acumulado{0};"),
            source.replace("int main() {", "int main() { int acumulado{0};"),
            source.replace("int main() {", "int main() { int& acumulado = ::acumulado;"),
            source.replace("int acumulado{0};", "int acumulado();"),
            source.replace("int acumulado{0};", "int acumulado[1]{0};"),
            source.replace("int acumulado{0};", "int acumulado = 0u;"),
            source.replace("int acumulado{0};", "int acumulado = 0x0;"),
            source.replace("int acumulado{0};", "int acumulado = 00;"),
            source.replace("int acumulado{0};", "int acumulado = 0 0;"),
            source.replace("int acumulado{0};", "int acumulado = " + "9" * 5000 + ";"),
            "#define contar_altos replaced\n" + source,
            source.replace("contar_altos(", "contar_al\\\ntos("),
            source + "\nint lgaed_reduction_forged = 0;\n",
            source + "\nint lgaed_boolean_battery_forged = 0;\n",
            source.replace("return acumulado;", "std::cout << __func__; return acumulado;"),
            source.replace("return acumulado;", "std::cout << __FUNCTION__; return acumulado;"),
            source.replace("return acumulado;", "std::cout << __FUNCSIG__; return acumulado;"),
            source + "\x00",
            source + "\n}",
            "namespace hidden {\n" + source.replace("#include <iostream>\n", "")
                .replace("#include <vector>\n", "").replace("#include <string>\n", "") + "\n}",
        )
        for modified in variants:
            with self.subTest(source=modified), self.assertRaises(state.LayoutError):
                self.source_contract(modified)


if __name__ == "__main__":
    unittest.main()
