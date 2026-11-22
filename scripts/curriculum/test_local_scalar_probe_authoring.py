"""Typed local variations keep one visible case and the original state contract."""
from copy import deepcopy
import hashlib
import json
from pathlib import Path
import tempfile
import unittest

import local_scalar_fixture_source as fixture
import stable_state_layout as state
from test_local_int_probe_authoring import fixture as int_fixture


def scalar_fixture():
    source = int_fixture()
    source["compilerLocalIntChecks"] = fixture.declarations()
    source["stateLayout"]["current"][0]["blocks"][0] = fixture.OWNER
    source["pages"][0]["blocks"][0]["outputCheck"]["cases"][0]["outputs"] = [
        "Pasillo: C\nAbierta: 1", "Pasillo: C\nAbierta: 1\n"]
    return source


class LocalScalarAuthoringTests(unittest.TestCase):
    def valid(self, document):
        state._validate_compiler_local_int_checks(document)

    def rejects(self, document):
        with self.assertRaises(state.LayoutError):
            self.valid(document)

    def test_explicit_char_and_bool_do_not_rewrite_the_visible_editor(self):
        document = scalar_fixture()
        original = deepcopy(document)
        self.valid(document)
        self.assertEqual(original, document)
        self.assertEqual(1, len(document["pages"][0]["blocks"][0]["outputCheck"]["cases"]))
        self.assertEqual(2, len(document["compilerLocalIntChecks"][0]["probes"]))

    def test_old_int_default_and_explicit_type_keep_their_contract(self):
        original = int_fixture()
        self.valid(original)
        explicit = deepcopy(original)
        explicit["compilerLocalIntChecks"][0]["probes"][0]["valueType"] = "int"
        self.valid(explicit)
        duplicate = deepcopy(original)
        duplicate["compilerLocalIntChecks"][0]["probes"].append(
            explicit["compilerLocalIntChecks"][0]["probes"][0])
        self.rejects(duplicate)
        for wrong in (False, True, "13", 13.0):
            item = deepcopy(original)
            item["compilerLocalIntChecks"][0]["probes"][0]["replacement"] = wrong
            self.rejects(item)

    def test_types_cannot_be_inferred_from_numbers_or_unknown_schema(self):
        for wrong in (None, False, 1, [], {}, "CHAR", "double", "", "signed char"):
            item = scalar_fixture()
            item["compilerLocalIntChecks"][0]["probes"][0]["valueType"] = wrong
            with self.subTest(valueType=wrong):
                self.rejects(item)
        item = scalar_fixture()
        item["compilerLocalIntChecks"][0]["probes"][0].pop("valueType")
        self.rejects(item)
        item = scalar_fixture()
        item["compilerLocalIntChecks"][0]["probes"][0]["valuetype"] = "char"
        self.rejects(item)

    def test_character_replacement_is_one_printable_ascii_value(self):
        for value in (" ", "'", "\\", "~", "D"):
            item = scalar_fixture()
            item["compilerLocalIntChecks"][0]["probes"][0]["replacement"] = value
            self.valid(item)
        for value in ("", "DC", "é", "🙂", "\x00", "\n", "\x7f", 68, True, None):
            item = scalar_fixture()
            item["compilerLocalIntChecks"][0]["probes"][0]["replacement"] = value
            with self.subTest(replacement=value):
                self.rejects(item)

    def test_boolean_replacement_is_a_json_boolean(self):
        for value in (False, True):
            item = scalar_fixture()
            item["compilerLocalIntChecks"][0]["probes"][1]["replacement"] = value
            self.valid(item)
        for value in (0, 1, "false", "true", "0", None, [], {}):
            item = scalar_fixture()
            item["compilerLocalIntChecks"][0]["probes"][1]["replacement"] = value
            with self.subTest(replacement=value):
                self.rejects(item)

    def test_update_witnesses_remain_int_only(self):
        for index in (0, 1):
            for fields in ({"afterUpdate": True}, {"afterUpdate": False},
                           {"expectedIncrease": 1},
                           {"afterUpdate": True, "expectedIncrease": 1}):
                item = scalar_fixture()
                item["compilerLocalIntChecks"][0]["probes"][index].update(fields)
                self.rejects(item)

    def test_one_variable_cannot_claim_two_types_or_repeat_a_probe(self):
        item = scalar_fixture()
        probes = item["compilerLocalIntChecks"][0]["probes"]
        probes.append(deepcopy(probes[0]))
        self.rejects(item)
        item = scalar_fixture()
        probes = item["compilerLocalIntChecks"][0]["probes"]
        probes.append({"variable": "pasillo", "replacement": 68,
                       "outputs": ["Pasillo: 68\nAbierta: 1"]})
        self.rejects(item)

    def test_hidden_outputs_must_distinguish_the_public_case(self):
        for values in (["Pasillo: C\nAbierta: 1"], ["Pasillo: C\r\nAbierta: 1\r\n"],
                       ["x", "x"], ["x\n", "x\r\n"], [], [""], ["\x00"]):
            item = scalar_fixture()
            item["compilerLocalIntChecks"][0]["probes"][0]["outputs"] = values
            self.rejects(item)

    def test_authentic_u2l8_adds_only_the_private_root_requirement(self):
        before, after = fixture.published_pair()
        self.assertEqual(state.validate(before), state.validate(after))
        self.assertEqual(before["pages"], after["pages"])
        self.assertEqual(before["stateLayout"], after["stateLayout"])
        self.assertEqual(before.get("textSemantics"), after.get("textSemantics"))
        old = state._blocks(before)[fixture.OWNER]
        self.assertEqual(old, state._blocks(after)[fixture.OWNER])
        self.assertEqual(1, len(old["outputCheck"]["cases"]))
        state.validate_transition(before, after)
        for wrong in (before, {**deepcopy(after), "compilerLocalIntChecks": []}):
            with self.assertRaises(state.LayoutError):
                state.validate_transition(after, wrong)
        for value in ("int", "bool"):
            wrong = deepcopy(after)
            probe = wrong["compilerLocalIntChecks"][0]["probes"][0]
            probe["valueType"] = value
            probe["replacement"] = 68 if value == "int" else False
            with self.assertRaises(state.LayoutError):
                state.validate_transition(after, wrong)

    def test_export_is_exact_repeatable_and_does_not_overwrite_a_conflict(self):
        authentic = fixture.published_bytes()
        with self.assertRaises(ValueError):
            fixture.verify_published_bytes(authentic + b" ")
        with tempfile.TemporaryDirectory(prefix="lgaed-scalar-qa-") as temp:
            output = Path(temp)
            manifest = fixture.export(output)
            self.assertEqual(authentic, (output / "before.json").read_bytes())
            self.assertEqual(fixture.PUBLISHED_SHA256,
                             hashlib.sha256(authentic).hexdigest())
            contents = json.loads(manifest.read_text(encoding="utf-8"))
            for name, digest in contents["files"].items():
                self.assertEqual(digest, hashlib.sha256((output / name).read_bytes()).hexdigest())
            before_times = {p.name: p.stat().st_mtime_ns for p in output.iterdir()}
            fixture.export(output)
            self.assertEqual(before_times, {p.name: p.stat().st_mtime_ns for p in output.iterdir()})
            (output / "after.json").write_bytes(b"existing QA")
            with self.assertRaises(FileExistsError):
                fixture.export(output)
            self.assertEqual(b"existing QA", (output / "after.json").read_bytes())
            self.assertEqual(authentic, (output / "before.json").read_bytes())


if __name__ == "__main__":
    unittest.main()
