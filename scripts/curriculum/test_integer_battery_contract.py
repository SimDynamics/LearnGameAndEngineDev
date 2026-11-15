"""Admit original CMake integer batteries without changing curricular assets."""
from __future__ import annotations

from copy import deepcopy
import json
from pathlib import Path
import unittest

import stable_state_layout as state

ROOT = Path(__file__).resolve().parents[2]
PATH = ROOT / (
    "app/src/main/assets/content/es/programming/programming_practice_tools/"
    "comprobaciones_repetibles/lesson_bateria_como_segundo_consumidor.json")
LEGACY_PATH = ROOT / (
    "app/src/main/assets/content/es/programming/programming_practice_tools/"
    "comprobaciones_repetibles/lesson_integrar_un_proyecto_y_sostener_su_evidencia.json")
ENVIO, LOTES = "block_0004_0003", "block_0010_0003"
OWNERS = ENVIO, LOTES


def integer_record(owner: str) -> dict:
    if owner == ENVIO:
        result = {
            "activity": owner, "kind": "cmake-int-unary-v1",
            "libraryPath": "src/cajas.cpp", "function": "cajas_para",
            "testPath": "pruebas/cajas_casos.cpp", "target": "comprobar_cajas",
            "publishedResult": "failure", "failureExitCode": "nonzero",
            "cases": [dict(input=value, expected=expected) for value, expected in
                      ((0, 0), (4, 1), (8, 2), (20, 5), (1, 1), (5, 2), (17, 5))],
            "prerequisite": {
                "target": "consultar_cajas",
                "calls": [dict(input=4, result=1), dict(input=8, result=2)],
                "standardInput": "", "standardOutput": "1:2",
                "standardError": "", "exitCode": 0,
            },
        }
    elif owner == LOTES:
        result = {
            "activity": owner, "kind": "cmake-int-unary-v1",
            "libraryPath": "motor/cantidad.cpp", "function": "lotes_para",
            "testPath": "pruebas/lotes_casos.cpp", "target": "verificar_lotes",
            "publishedResult": "success", "failureExitCode": 1,
            "cases": [dict(input=value, expected=expected) for value, expected in
                      ((0, 0), (6, 1), (7, 2), (18, 3))],
        }
    else:
        raise AssertionError("Unknown fixture owner")
    return result


def authorized(before: dict) -> dict:
    after = deepcopy(before)
    checks = after.setdefault("compilerOpenBatteryChecks", [])
    if type(checks) is not list:
        raise AssertionError("Published compilerOpenBatteryChecks must be an array")
    for owner in OWNERS:
        matches = [item for item in checks if type(item) is dict and item.get("activity") == owner]
        if len(matches) > 1:
            raise AssertionError("The published integer battery owner is duplicated")
        if not matches:
            checks.append(integer_record(owner))
    return after


def owned_record(document: dict, owner: str = ENVIO) -> dict:
    matches = [item for item in document["compilerOpenBatteryChecks"]
               if type(item) is dict and item.get("activity") == owner]
    if len(matches) != 1:
        raise AssertionError("The fixture needs exactly one requested battery owner")
    return matches[0]


def different_integer(value: int) -> int:
    return 0 if value != 0 else 1


class IntegerBatteryRootContractTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.original_bytes = PATH.read_bytes()
        cls.original = json.loads(cls.original_bytes)

    def declared(self):
        return authorized(self.original)

    def validate_record(self, record: dict, owner: str = ENVIO, block: dict | None = None):
        """Check an isolated authored contract against its real original owner."""
        state._validate_integer_battery_record(
            self.original, record, state._blocks(self.original)[owner] if block is None else block)

    def test_real_owners_keep_project_bytes_and_historical_addresses(self):
        for label, before in (("published", self.original),
                              ("already_integrated", self.declared())):
            with self.subTest(publication=label):
                after = authorized(before)
                self.assertEqual(state.validate(before), state.validate(after))
                state.validate_transition(before, after)
                self.assertEqual({key: value for key, value in before.items()
                                  if key != "compilerOpenBatteryChecks"},
                                 {key: value for key, value in after.items()
                                  if key != "compilerOpenBatteryChecks"})
                for owner in OWNERS:
                    self.assertEqual(state._blocks(before)[owner], state._blocks(after)[owner])
                self.assertEqual(after["stateLayout"]["current"][4]["blocks"][3], ENVIO)
                self.assertEqual(after["stateLayout"]["current"][10]["blocks"][3], LOTES)
                self.assertEqual(before["stateLayout"], after["stateLayout"])
                self.assertEqual(after, authorized(after))
        self.assertEqual(self.original_bytes, PATH.read_bytes())

    def test_new_declarations_append_and_published_contracts_remain_literal(self):
        before = self.declared()
        plan = deepcopy(before["stateLayout"]["current"])
        identity = "qa_other_integer_battery"
        plan[-1]["blocks"].append(identity)
        after = state.reorder(before, plan, [{
            "id": identity, "storage": {"page": 900000, "block": 0},
            "progressPage": plan[-1]["id"],
            "block": deepcopy(state._blocks(before)[LOTES]),
        }])
        record = deepcopy(owned_record(before, LOTES))
        record["activity"] = identity
        after["compilerOpenBatteryChecks"].append(record)
        state.validate_transition(before, after)
        prefix = len(before["compilerOpenBatteryChecks"])
        self.assertEqual(before["compilerOpenBatteryChecks"],
                         after["compilerOpenBatteryChecks"][:prefix])
        # A live publication may already contain these owners in another
        # declaration order, with other valid owners preceding them.
        existing = deepcopy(after)
        existing["compilerOpenBatteryChecks"].reverse()
        self.assertEqual(existing, authorized(existing))
        state.validate_transition(existing, authorized(existing))
        unintegrated = deepcopy(existing)
        unintegrated["compilerOpenBatteryChecks"] = [
            item for item in unintegrated["compilerOpenBatteryChecks"]
            if item["activity"] not in OWNERS]
        appended = authorized(unintegrated)
        size = len(unintegrated["compilerOpenBatteryChecks"])
        self.assertEqual(unintegrated["compilerOpenBatteryChecks"],
                         appended["compilerOpenBatteryChecks"][:size])
        state.validate_transition(unintegrated, appended)
        # Fixture setup also reuses a different valid published configuration;
        # authoring never replaces it with the test's preferred declaration.
        alternate = deepcopy(existing)
        current = owned_record(alternate, LOTES)
        current["cases"] = [dict(input=-2, expected=7), dict(input=3, expected=-1)]
        state.validate(alternate)
        self.assertEqual(alternate, authorized(alternate))
        state.validate_transition(alternate, authorized(alternate))

        changes = (
            ("removed", lambda d: d.pop("compilerOpenBatteryChecks")),
            ("owner_removed", lambda d: d["compilerOpenBatteryChecks"].remove(owned_record(d))),
            ("reordered", lambda d: d["compilerOpenBatteryChecks"].reverse()),
            ("expected", lambda d: owned_record(d)["cases"][0].update(
                expected=different_integer(owned_record(d)["cases"][0]["expected"]))),
            ("outcome", lambda d: owned_record(d).update(publishedResult=(
                "success" if owned_record(d)["publishedResult"] == "failure" else "failure"))),
            ("failure_policy", lambda d: owned_record(d).update(failureExitCode=(
                1 if owned_record(d)["failureExitCode"] == "nonzero" else "nonzero"))),
        )
        for label, change in changes:
            candidate = deepcopy(after)
            change(candidate)
            with self.subTest(change=label), self.assertRaises(state.LayoutError):
                state.validate_transition(after, candidate)

    def test_integer_variant_coexists_with_legacy_and_boolean_goals(self):
        before = json.loads(LEGACY_PATH.read_text(encoding="utf-8"))
        checks = before.setdefault("compilerOpenBatteryChecks", [])
        legacy_owner = "block_0006_0011"
        if not any(item.get("activity") == legacy_owner for item in checks):
            checks.append({
                "activity": legacy_owner, "library": "src/ajuste.cpp",
                "test": "pruebas/casos.cpp", "function": "intentar_ajuste",
                "comparison": "comprobar", "target": "comprobar_ajustes",
                "originalCases": [
                    dict(value=v, lower=lo, upper=hi, initial=initial)
                    for v, lo, hi, initial in (
                        (2, 3, 7, 77), (5, 3, 7, -8), (9, 3, 7, 19),
                        (4, 4, 4, -4), (-20, 20, 20, 8), (20, -20, -20, 17),
                        (0, 7, 3, 77), (21, 3, 7, -8), (-21, 3, 7, 19),
                        (0, -21, 7, -4), (0, 3, 21, 8))
                ],
            })
        state.validate(before)
        old_bytes = deepcopy(state._blocks(before)[legacy_owner])
        plan = deepcopy(before["stateLayout"]["current"])
        integer_id, boolean_id = "qa_integer_battery", "qa_boolean_battery"
        plan[-1]["blocks"].extend((integer_id, boolean_id))
        after = state.reorder(before, plan, [
            {"id": integer_id, "storage": {"page": 900000, "block": 0},
             "progressPage": plan[-1]["id"],
             "block": deepcopy(state._blocks(self.original)[LOTES])},
            {"id": boolean_id, "storage": {"page": 900000, "block": 1},
             "progressPage": plan[-1]["id"],
             "block": {"type": "cpp_compiler", "initialCode":
                       "bool different(int left, int right) { return left != right; }\n"
                       "int main() { return 0; }\n"}},
        ])
        integer = deepcopy(owned_record(self.declared(), LOTES))
        integer["activity"] = integer_id
        after["compilerOpenBatteryChecks"].extend((
            integer,
            {"activity": boolean_id, "kind": "boolean-pair-v1", "function": "different",
             "publishedResult": "success",
             "cases": [dict(first=1, second=2, expected=True)]},
        ))
        state.validate_transition(before, after)
        self.assertEqual(checks, after["compilerOpenBatteryChecks"][:len(checks)])
        self.assertEqual(old_bytes, state._blocks(after)[legacy_owner])

    def test_kind_fields_owner_and_result_are_closed(self):
        for label, change in (
            ("extra", lambda r: r.update(required=False)),
            ("missing", lambda r: r.pop("function")),
            ("unknown_kind", lambda r: r.update(kind="cmake-int-unary-v2")),
            ("untyped", lambda r: r.pop("kind")),
            ("nontext_kind", lambda r: r.update(kind=[])),
            ("unknown_owner", lambda r: r.update(activity="missing_owner")),
            ("non_editor", lambda r: r.update(activity="block_0000_0000")),
            ("unsafe_symbol", lambda r: r.update(function="cajas_para;exit")),
            ("unknown_function", lambda r: r.update(function="another_function")),
            ("main", lambda r: r.update(function="main")),
            ("bad_result", lambda r: r.update(publishedResult="nonzero")),
            ("boolean_result", lambda r: r.update(publishedResult=True)),
        ):
            candidate = self.declared()
            change(owned_record(candidate))
            with self.subTest(bad=label), self.assertRaises(state.LayoutError):
                state.validate(candidate)
        for checks in ([], {}, [None], [integer_record(ENVIO)] * 33):
            candidate = self.declared()
            candidate["compilerOpenBatteryChecks"] = checks
            with self.subTest(checks=type(checks).__name__, count=len(checks)), \
                    self.assertRaises(state.LayoutError):
                state.validate(candidate)
        repeated = self.declared()
        repeated["compilerOpenBatteryChecks"].append(deepcopy(owned_record(repeated)))
        with self.assertRaises(state.LayoutError):
            state.validate(repeated)
        for value in (True, False, 0, -1, 1.0, None, "1", "Nonzero", 2 ** 32):
            record = integer_record(LOTES)
            record["failureExitCode"] = value
            with self.subTest(failure_exit=value), self.assertRaises(state.LayoutError):
                self.validate_record(record, LOTES)
        for value in (1, 257, 2 ** 32 - 1, "nonzero"):
            record = integer_record(LOTES)
            record["failureExitCode"] = value
            self.validate_record(record, LOTES)

    def test_owner_requires_spanish_v2_unevaluated_cmake_and_exclusive_goal(self):
        for field, value in (("outputCheck", {}), ("compileCheck", {}),
                             ("sourceFiles", []), ("sourceFiles", ["src/cajas.cpp"]),
                             ("projectFiles", []), ("projectFiles", None),
                             ("cmake", None), ("language", ""), ("language", "cmake")):
            candidate = self.declared()
            state._blocks(candidate)[ENVIO][field] = value
            with self.subTest(field=field, value=value), self.assertRaisesRegex(
                    state.LayoutError, "unevaluated C\\+\\+ CMake"):
                state._validate_compiler_open_battery_checks(candidate)
        for field in ("cmake", "projectFiles"):
            candidate = self.declared()
            state._blocks(candidate)[ENVIO].pop(field)
            with self.subTest(absent=field), self.assertRaises(state.LayoutError):
                state._validate_compiler_open_battery_checks(candidate)
        absent_language = self.declared()
        state._blocks(absent_language)[ENVIO].pop("language", None)
        state._validate_compiler_open_battery_checks(absent_language)
        for field in ("compilerWorkflowChecks", "compilerRuntimeChecks",
                      "compilerNamedRuleChecks", "compilerCmakeGraphChecks",
                      "compilerReductionChecks"):
            candidate = self.declared()
            candidate[field] = [{"activity": ENVIO}]
            with self.subTest(conflict=field), self.assertRaisesRegex(state.LayoutError, "conflicts"):
                state._validate_compiler_open_battery_checks(candidate)
        for metadata in (None, {}, {"version": 1, "locale": "es"},
                         {"version": 2, "locale": "en"}):
            candidate = self.declared()
            candidate["stateLayout"] = metadata
            with self.subTest(metadata=metadata), self.assertRaisesRegex(state.LayoutError, "Spanish v2"):
                state._validate_compiler_open_battery_checks(candidate)
        for change in (
            lambda d: d.pop("stateLayout"),
            lambda d: d["stateLayout"].update(current=[]),
            lambda d: d["stateLayout"]["current"][4]["blocks"].remove(ENVIO),
        ):
            candidate = self.declared()
            change(candidate)
            with self.assertRaises(state.LayoutError):
                state.validate(candidate)

    def test_cases_use_strict_signed_integers_and_distinct_independent_inputs(self):
        for field in ("input", "expected"):
            for value in (True, False, 0.0, "0", None, -(2 ** 31) - 1, 2 ** 31):
                record = integer_record(LOTES)
                record["cases"][0][field] = value
                with self.subTest(field=field, value=value), self.assertRaises(state.LayoutError):
                    self.validate_record(record, LOTES)
        for cases in (
            [], None, {}, [dict(input=0)], [dict(input=0, expected=0, label="case")],
            [dict(input=0, expected=0), dict(input=0, expected=1)],
            [dict(input=index, expected=index) for index in range(33)],
        ):
            record = integer_record(LOTES)
            record["cases"] = cases
            with self.subTest(cases=cases), self.assertRaises(state.LayoutError):
                self.validate_record(record, LOTES)
        record = integer_record(LOTES)
        record["cases"] = [dict(input=-(2 ** 31), expected=2 ** 31 - 1)]
        self.validate_record(record, LOTES)
        record["cases"] = [dict(input=index, expected=31 - index) for index in range(32)]
        # Expectations are authored requirements, never calculated from /4,
        # /6, the published function or a hardcoded curricular owner.
        self.validate_record(record, LOTES)

    def test_project_paths_resolve_distinct_complete_original_files(self):
        for field in ("libraryPath", "testPath"):
            for path in (None, "", "../escape.cpp", "/absolute.cpp", "C:/test.cpp",
                         "dir\\file.cpp", "two files.cpp", "file.cc",
                         "a" * 253 + ".cpp", "missing.cpp", "file\x00.cpp"):
                record = integer_record(ENVIO)
                record[field] = path
                with self.subTest(field=field, path=path), self.assertRaises(state.LayoutError):
                    self.validate_record(record)
        same = integer_record(ENVIO)
        same["testPath"] = same["libraryPath"]
        with self.assertRaises(state.LayoutError):
            self.validate_record(same)
        original = state._blocks(self.original)[ENVIO]
        for path in ("src/cajas.cpp", "pruebas/cajas_casos.cpp", "CMakeLists.txt"):
            block = deepcopy(original)
            block["projectFiles"] = [entry for entry in block["projectFiles"] if entry["path"] != path]
            with self.subTest(missing=path), self.assertRaises(state.LayoutError):
                self.validate_record(integer_record(ENVIO), block=block)
        for malformed in (
            original["projectFiles"] + [deepcopy(original["projectFiles"][0])],
            original["projectFiles"] + [{"path": "cmakelists.txt", "initialCode": ""}],
            original["projectFiles"] + [{"path": "extra.h"}],
            original["projectFiles"] + [{"path": "extra.h", "initialCode": None}],
            original["projectFiles"] + [{"path": f"extra_{i}.h", "initialCode": ""} for i in range(128)],
        ):
            block = deepcopy(original)
            block["projectFiles"] = deepcopy(malformed)
            with self.subTest(files=len(malformed)), self.assertRaises(state.LayoutError):
                self.validate_record(integer_record(ENVIO), block=block)

    def test_prerequisite_binds_the_client_and_checker_targets(self):
        record = integer_record(ENVIO)
        original = state._blocks(self.original)[ENVIO]
        self.assertEqual(original["cmake"]["target"], record["prerequisite"]["target"])
        self.assertNotEqual(record["target"], original["cmake"]["target"])
        self.validate_record(record)
        # The initial selection can already be the checker; the client is
        # still a separate required preceding observation when declared.
        selected = deepcopy(original)
        selected["cmake"]["target"] = record["target"]
        self.validate_record(record, block=selected)
        without_client = deepcopy(record)
        without_client.pop("prerequisite")
        with self.assertRaises(state.LayoutError):
            self.validate_record(without_client)
        for field in ("target", "client"):
            for value in ("", "with-dash", "bad;target", "a" * 129, None,
                          "lgaed_integer_battery_private", "lgaed_boolean_battery_private"):
                changed = deepcopy(record)
                if field == "target":
                    changed["target"] = value
                else:
                    changed["prerequisite"]["target"] = value
                with self.subTest(field=field, value=value), self.assertRaises(state.LayoutError):
                    self.validate_record(changed)
        same = deepcopy(record)
        same["prerequisite"]["target"] = same["target"]
        with self.assertRaises(state.LayoutError):
            self.validate_record(same)
        unrelated = deepcopy(original)
        unrelated["cmake"]["target"] = "unrelated"
        with self.assertRaises(state.LayoutError):
            self.validate_record(record, block=unrelated)
        standalone = integer_record(LOTES)
        self.validate_record(standalone, LOTES)
        standalone["target"] = "consultar_lotes"
        with self.assertRaises(state.LayoutError):
            self.validate_record(standalone, LOTES)

    def test_prerequisite_fields_streams_and_exit_are_strict(self):
        for prerequisite in (None, [], {}, {"target": "consultar_cajas"}):
            record = integer_record(ENVIO)
            record["prerequisite"] = prerequisite
            with self.subTest(prerequisite=prerequisite), self.assertRaises(state.LayoutError):
                self.validate_record(record)
        for field in ("target", "calls", "standardInput", "standardOutput", "standardError", "exitCode"):
            record = integer_record(ENVIO)
            record["prerequisite"].pop(field)
            with self.subTest(missing=field), self.assertRaises(state.LayoutError):
                self.validate_record(record)
        extra = integer_record(ENVIO)
        extra["prerequisite"]["optional"] = True
        with self.assertRaises(state.LayoutError):
            self.validate_record(extra)
        for value in (True, False, 0.0, "0", 1, -1, None):
            record = integer_record(ENVIO)
            record["prerequisite"]["exitCode"] = value
            with self.subTest(exit=value), self.assertRaises(state.LayoutError):
                self.validate_record(record)
        for field in ("standardInput", "standardOutput", "standardError"):
            for value in (None, False, 0, "\x00", "\ud800", "é" * (2 * 1024 * 1024 + 1)):
                record = integer_record(ENVIO)
                record["prerequisite"][field] = value
                with self.subTest(field=field, value_type=type(value).__name__), \
                        self.assertRaises(state.LayoutError):
                    self.validate_record(record)
        mismatch = integer_record(ENVIO)
        mismatch["prerequisite"]["standardInput"] = "\n"
        with self.assertRaises(state.LayoutError):
            self.validate_record(mismatch)
        block = deepcopy(state._blocks(self.original)[ENVIO])
        block["initialStandardInput"] = "4\n8\n"
        exact = integer_record(ENVIO)
        exact["prerequisite"].update(
            standardInput="4\n8\n", standardOutput="uno:dos\r\n", standardError="aviso\n")
        self.validate_record(exact, block=block)

    def test_prerequisite_calls_are_ordered_bounded_and_may_repeat(self):
        record = integer_record(ENVIO)
        record["prerequisite"]["calls"] = [dict(input=4, result=1), dict(input=4, result=1)]
        self.validate_record(record)
        record["prerequisite"]["calls"] = [
            dict(input=-(2 ** 31), result=2 ** 31 - 1) for _ in range(32)]
        self.validate_record(record)
        for calls in ([], None, {}, [dict(input=4)], [dict(input=4, result=1, label=1)],
                      [dict(input=4, result=1)] * 33):
            changed = integer_record(ENVIO)
            changed["prerequisite"]["calls"] = calls
            with self.subTest(calls=calls), self.assertRaises(state.LayoutError):
                self.validate_record(changed)
        for field in ("input", "result"):
            for value in (True, False, 1.0, "1", None, -(2 ** 31) - 1, 2 ** 31):
                changed = integer_record(ENVIO)
                changed["prerequisite"]["calls"][0][field] = value
                with self.subTest(field=field, value=value), self.assertRaises(state.LayoutError):
                    self.validate_record(changed)
        # Literal publication guards also bind prerequisite observations.
        accepted = self.declared()
        current = owned_record(accepted)
        if "prerequisite" in current:
            for field in ("input", "result"):
                changed = deepcopy(accepted)
                first = owned_record(changed)["prerequisite"]["calls"][0]
                first[field] = different_integer(first[field])
                with self.subTest(rewrite=field), self.assertRaises(state.LayoutError):
                    state.validate_transition(accepted, changed)
            changed = deepcopy(accepted)
            owned_record(changed)["prerequisite"]["standardOutput"] += "\n"
            with self.assertRaises(state.LayoutError):
                state.validate_transition(accepted, changed)

    def test_function_accepts_comments_spacing_and_matching_prototypes(self):
        function = "count_boxes"
        original = "int count_boxes(int pieces) { return pieces; }\n"
        expected = state._integer_battery_definition(original, function)
        for source in (
            "#include <limits>\n" + original,
            "int /* value */ count_boxes ( int pieces ) {\n return /* keep */ pieces ; }\n",
            "int count_boxes(int pieces);\n" + original,
            "int count_boxes(int pieces);\n"
            "int other() { return count_boxes(4); }\n" + original,
            original + "int other() { return count_boxes(4); }\n",
            "int other() <% return 0; %>\n" + original,
        ):
            with self.subTest(source=source):
                self.assertEqual(expected, state._integer_battery_definition(source, function))
        # Private spellings and introspection text in comments/literals are
        # ordinary data; only actual identifiers affect the admission grammar.
        self.assertEqual(expected, state._integer_battery_definition(
            'const char* message="lgaed_integer_battery_trace __func__";\n' + original, function))

    def test_ambiguous_or_unsupported_function_source_is_rejected(self):
        function = "count_boxes"
        valid = "int count_boxes(int pieces) { return pieces; }\n"
        for source in (
            None, "", "\ud800", valid + "\x00", valid.replace("pieces;", "pieces\\\n;"),
            "#define answer 1\n" + valid,
            valid + "int lgaed_integer_battery_trace;",
            valid + "int lgaed_boolean_battery_trace;",
            "static " + valid, "inline " + valid, "constexpr " + valid,
            valid.replace(") {", ") noexcept {"),
            valid.replace("int pieces", "long pieces"),
            valid.replace("int pieces", "int"),
            valid.replace("int pieces", "int pieces, int more"),
            valid.replace("int count_boxes", "int& count_boxes"),
            valid.replace("int count_boxes", "int* count_boxes"),
            "namespace hidden { " + valid + "}\n",
            "struct Wrapper { " + valid + "};\n",
            valid + valid,
            valid + "int count_boxes(long pieces) { return 0; }\n",
            valid + "int& count_boxes(double pieces);\n",
            "int count_boxes(int another);\n" + valid,
            valid.replace("return pieces;", "return __func__[0];"),
            valid.replace("return pieces;", "return __FUNCTION__[0];"),
            valid.replace("return pieces;", "return __FUNCSIG__[0];"),
            valid[:-2], valid + "}", valid + " " * (4 * 1024 * 1024),
        ):
            with self.subTest(source_type=type(source).__name__,
                              prefix=repr(source)[:110]), self.assertRaises(state.LayoutError):
                state._integer_battery_definition(source, function)
        for name in ("main", "bad;name", "", None, "a" * 129, "lgaed_integer_battery_fn"):
            with self.subTest(name=name), self.assertRaises(state.LayoutError):
                state._integer_battery_definition(valid, name)


if __name__ == "__main__":
    unittest.main()
