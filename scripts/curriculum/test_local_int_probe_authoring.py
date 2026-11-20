"""Isolated authoring checks for the opt-in hidden local-int dependency contract."""
from copy import deepcopy
import json
from pathlib import Path
import unittest

import stable_state_layout as state


def fixture():
    return {
        "stateLayout": {
            "version": 2, "locale": "es",
            "current": [{"blocks": ["block_0004_0002"]}],
        },
        "pages": [{"blocks": [{
            "type": "cpp_compiler",
            "language": "cpp",
            "initialCode": "#include <iostream>\nint main(){int llaves{7};}\n",
            "outputCheck": {"cases": [{
                "standardInput": "",
                "outputs": ["Llaves: 7", "Llaves: 7\n"],
            }]},
        }]}],
        "compilerLocalIntChecks": [{
            "activity": "block_0004_0002",
            "probes": [{
                "variable": "llaves",
                "replacement": 13,
                "outputs": ["Llaves: 13", "Llaves: 13\n"],
            }],
        }],
    }


class LocalIntAuthoringTests(unittest.TestCase):
    def valid(self, item):
        state._validate_compiler_local_int_checks(item)

    def rejected(self, item):
        with self.assertRaises(state.LayoutError):
            self.valid(item)

    def test_valid_declarations_are_root_only(self):
        document = fixture()
        snapshot = deepcopy(document["pages"])
        self.valid(document)
        self.assertEqual(document["pages"], snapshot)
        self.assertIn("compilerLocalIntChecks", state.FLOW_FIELDS)
        document["compilerLocalIntChecks"][0]["probes"].append({
            "variable": "llaves", "replacement": -20,
            "outputs": ["Llaves: -20"],
        })
        self.valid(document)

    def test_invalid_owners_and_schema(self):
        for field, value in [
            ("activity", "missing_editor"),
            ("activity", "block_0004_0002/unsafe"),
            ("probes", []),
            ("probes", [{}]),
            ("probes", [{"variable": "llaves", "replacement": True,
                         "outputs": ["Llaves: 13"]}]),
        ]:
            with self.subTest(field=field, value=value):
                item = fixture()
                item["compilerLocalIntChecks"][0][field] = value
                self.rejected(item)
        other = fixture()
        other["stateLayout"]["locale"] = "en"
        self.rejected(other)
        other = fixture()
        other["pages"][0]["blocks"][0]["projectFiles"] = []
        self.rejected(other)

    def test_rejects_trivial_or_forged_variations(self):
        for key, value in [
            ("variable", "llaves; return 0"),
            ("variable", "__secret"),
            ("variable", "main"),
            ("replacement", 2 ** 31),
            ("replacement", -2 ** 31 - 1),
            ("replacement", 13.0),
            ("outputs", ["Llaves: 7"]),
            ("outputs", ["Llaves: 7\r\n"]),
            ("outputs", ["Llaves: 13", "Llaves: 13"]),
            ("outputs", ["Llaves: 13\n", "Llaves: 13\r\n"]),
            ("outputs", ["\x00"]),
            ("outputs", []),
            ("outputs", ["x" * 4097]),
        ]:
            with self.subTest(key=key, value=str(value)[:40]):
                item = fixture()
                item["compilerLocalIntChecks"][0]["probes"][0][key] = value
                self.rejected(item)

    def test_existing_lesson_root_is_append_only_without_changing_old_editor(self):
        path = (Path(__file__).resolve().parents[2] /
                "app/src/main/assets/content/es/programming/cpp_fundamentals/"
                "objetos_y_estado/lesson_objetos_valores_y_tipos.json")
        before = json.loads(path.read_text(encoding="utf-8-sig"))
        after = deepcopy(before)
        after["compilerLocalIntChecks"] = [{
            "activity": "block_0004_0002",
            "probes": [{"variable": "llaves", "replacement": 13,
                        "outputs": ["Llaves: 13", "Llaves: 13\n"]}],
        }]
        state.validate_transition(before, after)
        self.assertEqual(before["pages"], after["pages"])
        self.assertEqual(before["stateLayout"], after["stateLayout"])
        expanded = deepcopy(after)
        expanded["compilerLocalIntChecks"].append({
            "activity": "block_0004_0006",
            "probes": [{"variable": "llaves", "replacement": 19,
                        "outputs": ["Llaves: 19"]}],
        })
        state.validate_transition(after, expanded)
        for altered in (
            before,
            {**deepcopy(expanded),
             "compilerLocalIntChecks": expanded["compilerLocalIntChecks"][::-1]},
            {**deepcopy(expanded),
             "compilerLocalIntChecks": [dict(
                 expanded["compilerLocalIntChecks"][0],
                 probes=[{"variable": "llaves", "replacement": 14,
                          "outputs": ["Llaves: 14"]}])]},
        ):
            with self.subTest(altered=str(altered.get("compilerLocalIntChecks"))[:45]):
                with self.assertRaises(state.LayoutError):
                    state.validate_transition(expanded, altered)


    def test_original_and_changed_increase_are_independent_contracts(self):
        document = fixture()
        document["compilerLocalIntChecks"][0]["probes"].append({
            "variable": "llaves", "replacement": 13, "afterUpdate": True,
            "outputs": ["Llaves: 17", "Llaves: 17\n"],
        })
        self.valid(document)
        for value in (False, None, "true", 1, 0):
            wrong = deepcopy(document)
            wrong["compilerLocalIntChecks"][0]["probes"][1]["afterUpdate"] = value
            with self.subTest(afterUpdate=value):
                self.rejected(wrong)
        wrong = deepcopy(document)
        wrong["compilerLocalIntChecks"][0]["probes"].append(
            deepcopy(wrong["compilerLocalIntChecks"][0]["probes"][1]))
        self.rejected(wrong)

    def test_rejects_duplicate_edits_and_conflicting_owners(self):
        item = fixture()
        item["compilerLocalIntChecks"][0]["probes"].append(deepcopy(
            item["compilerLocalIntChecks"][0]["probes"][0]))
        self.rejected(item)
        item = fixture()
        item["compilerLocalIntChecks"].append(deepcopy(
            item["compilerLocalIntChecks"][0]))
        self.rejected(item)
        item = fixture()
        item["compilerNamedRuleChecks"] = [{"activity": "block_0004_0002"}]
        self.rejected(item)
        item = fixture()
        item["pages"][0]["blocks"][0]["outputCheck"]["cases"].append({
            "standardInput": "5", "outputs": ["Llaves: 5"]
        })
        self.rejected(item)


if __name__ == "__main__":
    unittest.main()
