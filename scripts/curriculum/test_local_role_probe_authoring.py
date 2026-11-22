"""Name-independent local roles preserve one public U2L10 check and history."""
from copy import deepcopy
import hashlib
import json
from pathlib import Path
import tempfile
import unittest

import local_role_fixture_source as fixture
import stable_state_layout as state


def role_fixture():
    outputs = ("Puesto 03\nMesas: 6\nCable: 3.75 m", "Modulo 02\nInicio: 7\nAhora: 10")
    return {
        "stateLayout": {"version": 2, "locale": "es",
                        "current": [{"blocks": list(fixture.OWNERS)}]},
        "pages": [{"blocks": [
            {"type": "cpp_compiler", "initialCode": "#include <iostream>\nint main(){}\n",
             "outputCheck": {"inputComparison": "exact", "cases": [
                 {"standardInput": "", "outputs": fixture.alternatives(output)}]}}
            for output in outputs]}],
        "compilerLocalRoleChecks": fixture.declarations(),
    }


class LocalRoleAuthoringTests(unittest.TestCase):
    def valid(self, document):
        state._validate_compiler_local_role_checks(document)

    def rejects(self, document):
        with self.assertRaises(state.LayoutError):
            self.valid(document)

    def test_roles_are_root_only_and_do_not_impose_source_names(self):
        document = role_fixture()
        original = deepcopy(document)
        self.valid(document)
        self.assertEqual(original, document)
        self.assertIn("compilerLocalRoleChecks", state.FLOW_FIELDS)
        self.assertEqual([1, 1], [len(block["outputCheck"]["cases"])
                                 for block in document["pages"][0]["blocks"]])
        self.assertEqual([3, 3], [sum(len(role["probes"]) for role in entry["roles"])
                                 for entry in document["compilerLocalRoleChecks"]])
        # Logical role IDs do not bind the editor to the reference's names.
        document["compilerLocalRoleChecks"][0]["roles"][0]["id"] = "freely_named_role"
        self.valid(document)

    def test_exact_schemas_and_safe_bounded_role_ids(self):
        for mutation in (
            lambda entry: entry.update(extra=True),
            lambda entry: entry.pop("activity"),
            lambda entry: entry.update(activity="missing_editor"),
            lambda entry: entry["roles"][0].update(variable="tramo_a"),
            lambda entry: entry["roles"][0].pop("initialValue"),
            lambda entry: entry["roles"][0]["probes"][0].update(extra=True),
            lambda entry: entry["roles"][0]["probes"][0].pop("replacement"),
            lambda entry: entry["roles"][0]["probes"][0].pop("outputs"),
        ):
            document = role_fixture()
            mutation(document["compilerLocalRoleChecks"][0])
            self.rejects(document)
        for identity in ("", "0first", "a/b", "ñ", "x" * 65, False, None):
            document = role_fixture()
            document["compilerLocalRoleChecks"][0]["roles"][0]["id"] = identity
            self.rejects(document)
        for identity in ("_", "a" * 64, "Data_01"):
            document = role_fixture()
            document["compilerLocalRoleChecks"][0]["roles"][0]["id"] = identity
            self.valid(document)
        for value in ("float", "char", "bool", "DOUBLE", "", False, None):
            document = role_fixture()
            document["compilerLocalRoleChecks"][0]["roles"][0]["valueType"] = value
            self.rejects(document)

    def test_only_existing_empty_input_single_case_cpp_owners_are_admitted(self):
        for field, value in (("type", "code"), ("language", "c"), ("initialCode", ""),
                             ("initialStandardInput", "7"), ("compileCheck", {}),
                             ("cmake", {}), ("projectFiles", []), ("sourceFiles", [])):
            document = role_fixture()
            document["pages"][0]["blocks"][0][field] = value
            self.rejects(document)
        for field, value in (("standardInput", "1"), ("exitCode", 1), ("exitCode", True),
                             ("target", "other"), ("standardError", "nonBlank"),
                             ("standardErrorExact", "")):
            document = role_fixture()
            document["pages"][0]["blocks"][0]["outputCheck"]["cases"][0][field] = value
            self.rejects(document)
        document = role_fixture()
        cases = document["pages"][0]["blocks"][0]["outputCheck"]["cases"]
        cases.append(deepcopy(cases[0]))
        self.rejects(document)
        for field, value in (("version", 1), ("locale", "en")):
            document = role_fixture()
            document["stateLayout"][field] = value
            self.rejects(document)

    def test_owner_role_and_total_probe_limits(self):
        for value in ([], {}, None, False):
            document = role_fixture()
            document["compilerLocalRoleChecks"] = value
            self.rejects(document)
        document = role_fixture()
        document["compilerLocalRoleChecks"].append(deepcopy(document["compilerLocalRoleChecks"][0]))
        self.rejects(document)
        for count in (0, 5):
            document = role_fixture()
            document["compilerLocalRoleChecks"][0]["roles"] = [
                {"id": f"role{index}", "valueType": "double", "initialValue": 1.5,
                 "probes": [{"replacement": 2.5, "outputs": ["private"]}]}
                for index in range(count)]
            self.rejects(document)
        document = role_fixture()
        roles = document["compilerLocalRoleChecks"][0]["roles"]
        roles.append(deepcopy(roles[0]))
        self.rejects(document)  # Duplicate role ID within the allowed four.
        roles[-1]["id"] = "fourth"
        self.valid(document)
        document = role_fixture()
        roles = document["compilerLocalRoleChecks"][0]["roles"]
        roles[0]["probes"] = [{"replacement": 3 + index, "outputs": [f"private {index}"]}
                               for index in range(6)]
        self.valid(document)  # Six plus one plus one.
        roles[0]["probes"].append({"replacement": 12, "outputs": ["ninth"]})
        self.rejects(document)
        document = role_fixture()
        document["compilerLocalRoleChecks"][0]["roles"][0]["probes"] = []
        self.rejects(document)
        template = role_fixture()
        block = template["pages"][0]["blocks"][0]
        declaration = template["compilerLocalRoleChecks"][0]
        template["pages"][0]["blocks"] = [deepcopy(block) for _ in range(32)]
        names = [f"owner_{index}" for index in range(32)]
        template["stateLayout"]["current"][0]["blocks"] = names
        template["compilerLocalRoleChecks"] = [dict(deepcopy(declaration), activity=name)
                                                for name in names]
        self.valid(template)
        template["compilerLocalRoleChecks"].append(dict(deepcopy(declaration), activity="owner_32"))
        self.rejects(template)

    def test_finite_typed_values_and_int32_or_double_bounds(self):
        for owner_index, limit in ((0, 1_000_000), (1, 2 ** 31 - 1)):
            for field in ("initialValue", "replacement"):
                for bad in (True, False, None, "7", {}, [], float("nan"), float("inf"), -float("inf"), limit + 1):
                    document = role_fixture()
                    role = document["compilerLocalRoleChecks"][owner_index]["roles"][0]
                    target = role if field == "initialValue" else role["probes"][0]
                    target[field] = bad
                    with self.subTest(type=role["valueType"], field=field, value=bad):
                        self.rejects(document)
            for value in (-limit, limit):
                document = role_fixture()
                document["compilerLocalRoleChecks"][owner_index]["roles"][0]["initialValue"] = value
                self.valid(document)
        for field in ("initialValue", "replacement"):
            document = role_fixture()
            role = document["compilerLocalRoleChecks"][1]["roles"][0]
            target = role if field == "initialValue" else role["probes"][0]
            target[field] = -2 ** 31
            self.valid(document)
            target[field] = -2 ** 31 - 1
            self.rejects(document)
            target[field] = 7.5
            self.rejects(document)
        document = role_fixture()
        roles = document["compilerLocalRoleChecks"][1]["roles"]
        roles[0]["initialValue"] = 7.0
        roles[0]["probes"][0]["replacement"] = 11.0
        roles[1]["probes"][0]["expectedIncrease"] = 3.0
        self.valid(document)

    def test_initial_and_snapshot_changes_are_nontrivial_and_updates_are_nonzero(self):
        for owner_index, role_index in ((0, 0), (1, 0), (1, 2)):
            document = role_fixture()
            role = document["compilerLocalRoleChecks"][owner_index]["roles"][role_index]
            role["probes"][0]["replacement"] = role["initialValue"]
            self.rejects(document)
        document = role_fixture()
        current = document["compilerLocalRoleChecks"][1]["roles"][1]["probes"][0]
        current["replacement"] = 0
        self.rejects(document)
        current["replacement"] = -2
        self.valid(document)

    def test_after_update_requires_a_real_exact_int_increase(self):
        for change in (
            lambda probe: probe.pop("expectedIncrease"),
            lambda probe: probe.pop("afterUpdate"),
            lambda probe: probe.update(afterUpdate=False),
            lambda probe: probe.update(afterUpdate=1),
            lambda probe: probe.update(beforeUpdateOf="initial"),
        ):
            document = role_fixture()
            change(document["compilerLocalRoleChecks"][1]["roles"][1]["probes"][0])
            self.rejects(document)
        for increase in (0, -1, 2 ** 31, 3.5, True, "3", None):
            document = role_fixture()
            document["compilerLocalRoleChecks"][1]["roles"][1]["probes"][0]["expectedIncrease"] = increase
            self.rejects(document)
        document = role_fixture()
        document["compilerLocalRoleChecks"][1]["roles"][1]["probes"][0]["expectedIncrease"] = 2 ** 31 - 1
        self.valid(document)
        document = role_fixture()
        document["compilerLocalRoleChecks"][0]["roles"][0]["probes"][0].update(
            afterUpdate=True, expectedIncrease=3)
        self.rejects(document)

    def test_snapshot_relations_are_typed_complete_and_independent_of_role_order(self):
        document = role_fixture()
        document["compilerLocalRoleChecks"][1]["roles"].reverse()
        self.valid(document)  # A reference may point to a later authored role.
        for target in ("missing", "saved", "initial", "", False, None):
            document = role_fixture()
            document["compilerLocalRoleChecks"][1]["roles"][2]["probes"][0]["beforeUpdateOf"] = target
            self.rejects(document)
        document = role_fixture()
        roles = document["compilerLocalRoleChecks"][1]["roles"]
        roles[2]["valueType"] = "double"
        self.rejects(document)
        document = role_fixture()
        roles = document["compilerLocalRoleChecks"][1]["roles"]
        roles[0]["probes"][0].update(afterUpdate=True, expectedIncrease=3)
        roles[1]["probes"].append({"beforeUpdateOf": "initial", "replacement": 17, "outputs": ["private"]})
        self.rejects(document)  # The saved role cannot depend on another dependent role.

    def test_private_outputs_are_bounded_exact_and_distinguish_the_public_case(self):
        for values in ([], [""], ["\x00"], ["x" * 4097], ["é" * 2049], [False],
                       ["a", "b", "c", "d", "e"], ["x", "x"], ["x\n", "x\r\n"],
                       ["Puesto 03\r\nMesas: 6\r\nCable: 3.75 m\r\n"]):
            document = role_fixture()
            document["compilerLocalRoleChecks"][0]["roles"][0]["probes"][0]["outputs"] = values
            self.rejects(document)
        for values in (["x" * 4096], ["é" * 2048], ["x", "x\n", "x\r", " x"]):
            document = role_fixture()
            document["compilerLocalRoleChecks"][0]["roles"][0]["probes"][0]["outputs"] = values
            self.valid(document)

    def test_other_private_objectives_cannot_share_an_owner(self):
        for key in ("compilerInputChecks", "compilerLocalIntChecks", "compilerLocalStringChecks",
                    "compilerWorkflowChecks", "compilerRuntimeChecks", "compilerNamedRuleChecks",
                    "compilerOpenBatteryChecks", "compilerCmakeGraphChecks", "compilerDualInterfaceChecks"):
            document = role_fixture()
            document[key] = [{"activity": fixture.OWNERS[0]}]
            self.rejects(document)

    def test_raw_duplicate_contract_fields_are_rejected(self):
        raw = json.dumps(role_fixture(), ensure_ascii=False, separators=(",", ":"))
        for old, new in (
            ('"compilerLocalRoleChecks":', '"compilerLocalRoleChecks":[],"compilerLocalRoleChecks":'),
            ('"id":"first"', '"id":"wrong","id":"first"'),
            ('"initialValue":1.5', '"initialValue":99,"initialValue":1.5'),
            ('"valueType":"double"', '"valueType":"int","valueType":"double"'),
            ('"replacement":2.5', '"replacement":99,"replacement":2.5'),
            ('"beforeUpdateOf":"current"', '"beforeUpdateOf":"wrong","beforeUpdateOf":"current"'),
        ):
            self.assertIn(old, raw)
            with self.assertRaises((state.LayoutError, ValueError)):
                state.parse_json(raw.replace(old, new, 1))

    def test_authentic_main_publication_keeps_all_blocks_history_and_editorial_revision(self):
        before, after = fixture.published_pair()
        self.assertEqual(state.validate(before), state.validate(after))
        for key in before:
            self.assertEqual(before[key], after[key], key)
        self.assertEqual(54, len(before["stateLayout"]["editorial"]))
        self.assertEqual(1, len(before["stateLayout"]["choiceRevisions"]))
        self.assertEqual(6, len(before["stateLayout"]["retired"]))
        self.assertEqual(1, len(before["stateLayout"]["retirementHistory"]))
        blocks = state._blocks(before)
        for identity, digest in fixture.REFERENCE_SHA256.items():
            self.assertEqual(digest, hashlib.sha256(blocks[identity]["code"].encode()).hexdigest())
        retired = {entry["id"]: entry["block"] for entry in before["stateLayout"]["retired"]}
        for identity, digest in fixture.ARCHIVED_SHA256.items():
            self.assertEqual(digest, hashlib.sha256(retired[identity]["code"].encode()).hexdigest())
        self.assertEqual([1, 1], [len(blocks[identity]["outputCheck"]["cases"])
                                 for identity in fixture.OWNERS])
        self.assertEqual(fixture.declarations(), after["compilerLocalRoleChecks"])

    def test_local_role_declarations_are_append_only(self):
        before, after = fixture.published_pair()
        for mutation in (
            lambda doc: doc.pop("compilerLocalRoleChecks"),
            lambda doc: doc.update(compilerLocalRoleChecks=[]),
            lambda doc: doc["compilerLocalRoleChecks"].reverse(),
            lambda doc: doc["compilerLocalRoleChecks"][0]["roles"].reverse(),
            lambda doc: doc["compilerLocalRoleChecks"][0]["roles"][0].update(initialValue=1.75),
            lambda doc: doc["compilerLocalRoleChecks"][0]["roles"][0]["probes"][0].update(replacement=3.5),
            lambda doc: doc["compilerLocalRoleChecks"][1]["roles"][2]["probes"][0].pop("beforeUpdateOf"),
        ):
            altered = deepcopy(after)
            mutation(altered)
            with self.assertRaises(state.LayoutError):
                state.validate_transition(after, altered)
        partial = deepcopy(before)
        partial["compilerLocalRoleChecks"] = fixture.declarations()[:1]
        state.validate_transition(before, partial)
        state.validate_transition(partial, after)

    def test_export_is_byte_pinned_repeatable_and_never_overwrites_a_conflict(self):
        authentic = fixture.published_bytes()
        self.assertEqual(fixture.PUBLISHED_SHA256, hashlib.sha256(authentic).hexdigest())
        with self.assertRaises(ValueError):
            fixture.verify_published_bytes(authentic + b" ")
        with tempfile.TemporaryDirectory(prefix="lgaed-local-roles-qa-") as temp:
            output = Path(temp)
            manifest = fixture.export(output)
            self.assertEqual(authentic, (output / "before.json").read_bytes())
            contents = json.loads(manifest.read_text(encoding="utf-8"))
            self.assertEqual(list(fixture.OWNERS), contents["activities"])
            self.assertEqual(fixture.REFERENCE_SHA256, contents["referenceSourceSha256"])
            for name, digest in contents["files"].items():
                self.assertEqual(digest, hashlib.sha256((output / name).read_bytes()).hexdigest())
            timestamps = {p.name: p.stat().st_mtime_ns for p in output.iterdir()}
            fixture.export(output)
            self.assertEqual(timestamps, {p.name: p.stat().st_mtime_ns for p in output.iterdir()})
            (output / "after.json").write_bytes(b"existing QA")
            with self.assertRaises(FileExistsError):
                fixture.export(output)
            self.assertEqual(b"existing QA", (output / "after.json").read_bytes())
            self.assertEqual(authentic, (output / "before.json").read_bytes())


if __name__ == "__main__":
    unittest.main()
