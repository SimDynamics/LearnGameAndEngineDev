"""Private input sensitivity leaves authored public cases and history intact."""
from copy import deepcopy
import hashlib
import json
from pathlib import Path
import tempfile
import unittest

import input_dependency_fixture_source as fixture
import stable_state_layout as state


def input_fixture():
    return {
        "stateLayout": {"version": 2, "locale": "es",
                        "current": [{"blocks": list(fixture.OWNERS)}]},
        "pages": [{"blocks": [
            {"type": "cpp_compiler", "language": "cpp",
             "initialCode": "#include <iostream>\nint main(){}\n",
             "initialStandardInput": "Rita 4",
             "outputCheck": {"inputComparison": "tokens", "cases": [
                 {"standardInput": "Rita 4", "outputs": ["Rita: 7", "Rita: 7\n"]},
                 {"standardInput": "Leo 2", "outputs": ["Leo: 5", "Leo: 5\n"]},
             ]}},
            {"type": "cpp_compiler", "language": "cpp",
             "initialCode": "#include <iostream>\nint main(){}\n",
             "initialStandardInput": "Iris 4 2",
             "outputCheck": {"inputComparison": "tokens", "cases": [
                 {"standardInput": "Iris 4 2", "outputs": ["Iris: 22", "Iris: 22\n"]},
                 {"standardInput": "Leo 0 4", "outputs": ["Leo: 4", "Leo: 4\n"]},
                 {"standardInput": "Vera 3 1", "outputs": ["Vera: 16", "Vera: 16\n"]},
                 {"standardInput": "Sol 20 4", "outputs": ["Sol: 104", "Sol: 104\n"]},
             ]}},
        ]}],
        "compilerInputChecks": fixture.declarations(),
    }


class InputDependencyAuthoringTests(unittest.TestCase):
    def valid(self, document):
        state._validate_compiler_input_checks(document)

    def rejects(self, document):
        with self.assertRaises(state.LayoutError):
            self.valid(document)

    def test_two_public_batteries_are_untouched_and_optional_inputs_can_be_private(self):
        document = input_fixture()
        before = deepcopy(document)
        self.valid(document)
        self.assertEqual(before, document)
        self.assertEqual([2, 4], [len(b["outputCheck"]["cases"])
                                 for b in document["pages"][0]["blocks"]])
        self.assertEqual("Leo 2", document["compilerInputChecks"][0]["probes"][0]["standardInput"])
        self.assertIn("compilerInputChecks", state.FLOW_FIELDS)

    def test_root_and_probe_schemas_are_exact(self):
        for change in (lambda e: e.update(extra=True),
                       lambda e: e.pop("activity"),
                       lambda e: e.update(activity="missing_editor"),
                       lambda e: e.update(activity="unsafe/owner"),
                       lambda e: e.update(probes={}),
                       lambda e: e["probes"][0].update(extra=True),
                       lambda e: e["probes"][0].pop("outputs"),
                       lambda e: e["probes"][0].pop("standardInput")):
            document = input_fixture()
            change(document["compilerInputChecks"][0])
            self.rejects(document)
        for invalid in (None, {}, [], True):
            document = input_fixture()
            document["compilerInputChecks"] = invalid
            self.rejects(document)
        for field, value in (("version", 1), ("locale", "en")):
            document = input_fixture()
            document["stateLayout"][field] = value
            self.rejects(document)

    def test_only_existing_single_file_output_editors_can_own_checks(self):
        for field, value in (("type", "code"), ("language", "c"), ("initialCode", ""),
                             ("outputCheck", {}), ("compileCheck", {}), ("cmake", {}),
                             ("projectFiles", []), ("sourceFiles", [])):
            document = input_fixture()
            document["pages"][0]["blocks"][0][field] = value
            with self.subTest(field=field):
                self.rejects(document)
        for field, value in (("initialStandardInput", "different"),):
            document = input_fixture()
            document["pages"][0]["blocks"][0][field] = value
            self.rejects(document)
        for field, value in (("exitCode", 1), ("exitCode", True), ("target", "other"),
                             ("standardError", ""), ("standardErrorExact", True)):
            document = input_fixture()
            document["pages"][0]["blocks"][0]["outputCheck"]["cases"][0][field] = value
            self.rejects(document)

    def test_owner_and_probe_counts_are_bounded_without_repeated_owners(self):
        document = input_fixture()
        document["compilerInputChecks"].append(deepcopy(document["compilerInputChecks"][0]))
        self.rejects(document)
        for count in (0, 9):
            document = input_fixture()
            document["compilerInputChecks"][0]["probes"] = [
                {"standardInput": f"Probe {i}", "outputs": [f"Result {i}"]}
                for i in range(count)]
            self.rejects(document)
        document = input_fixture()
        document["compilerInputChecks"][0]["probes"] = [
            {"standardInput": f"Probe {i}", "outputs": [f"Result {i}"]} for i in range(8)]
        self.valid(document)
        template = deepcopy(document["pages"][0]["blocks"][0])
        document["pages"][0]["blocks"] = [deepcopy(template) for _ in range(32)]
        identities = [f"input_owner_{index}" for index in range(32)]
        document["stateLayout"]["current"][0]["blocks"] = identities
        document["compilerInputChecks"] = [
            {"activity": identity, "probes": [{"standardInput": "Leo 2", "outputs": ["Leo: 5"]}]}
            for identity in identities]
        self.valid(document)
        document["compilerInputChecks"].append({"activity": "thirty_third", "probes": []})
        self.rejects(document)

    def test_inputs_use_exact_bytes_or_the_six_ascii_token_separators(self):
        for value in ("Rita 4", " \tRita\r\n4\v\f", "Leo\t2\r\n"):
            document = input_fixture()
            document["compilerInputChecks"][0]["probes"][1]["standardInput"] = value
            self.rejects(document)
        document = input_fixture()
        document["compilerInputChecks"][0]["probes"][1]["standardInput"] = "Rita\u00a04"
        self.valid(document)  # NBSP is data, not a C++ token separator.
        document["pages"][0]["blocks"][0]["outputCheck"]["inputComparison"] = "exact"
        document["compilerInputChecks"][0]["probes"][1]["standardInput"] = "Rita\t4"
        self.valid(document)
        document["compilerInputChecks"][0]["probes"][2]["standardInput"] = "Rita\t4"
        self.rejects(document)
        document = input_fixture()
        document["pages"][0]["blocks"][0]["outputCheck"]["inputComparison"] = "unicode_tokens"
        self.rejects(document)

    def test_inputs_and_outputs_are_strict_strings_with_utf8_byte_limits(self):
        for field in ("standardInput", "outputs"):
            for value in (False, 4, None, {}, [], "\x00", "x" * 4097, "é" * 2049):
                document = input_fixture()
                probe = document["compilerInputChecks"][0]["probes"][0]
                probe[field] = [value] if field == "outputs" else value
                with self.subTest(field=field, value=str(value)[:24]):
                    self.rejects(document)
            for value in ("", "x" * 4096, "é" * 2048):
                document = input_fixture()
                probe = document["compilerInputChecks"][0]["probes"][0]
                probe[field] = [value] if field == "outputs" else value
                self.valid(document)

    def test_outputs_distinguish_the_first_case_and_only_normalize_crlf(self):
        for outputs in ([], ["a", "b", "c", "d", "e"], "different",
                        ["Rita: 7"], ["Rita: 7\r\n"], ["x", "x"], ["x\n", "x\r\n"]):
            document = input_fixture()
            document["compilerInputChecks"][0]["probes"][0]["outputs"] = outputs
            self.rejects(document)
        document = input_fixture()
        document["compilerInputChecks"][0]["probes"][0]["outputs"] = ["x", "x\n", "x\r", " x"]
        self.valid(document)

    def test_an_owner_cannot_share_another_private_runtime_goal(self):
        for key in ("compilerLocalIntChecks", "compilerLocalStringChecks", "compilerWorkflowChecks",
                    "compilerRuntimeChecks", "compilerNamedRuleChecks", "compilerOpenBatteryChecks",
                    "compilerReductionChecks", "compilerCmakeGraphChecks", "compilerDualInterfaceChecks"):
            document = input_fixture()
            document[key] = [{"activity": fixture.OWNERS[0]}]
            with self.subTest(root=key):
                self.rejects(document)

    def test_raw_duplicate_fields_are_rejected_before_schema_interpretation(self):
        raw = json.dumps(input_fixture(), ensure_ascii=False, separators=(",", ":"))
        for old, new in (
            ('"compilerInputChecks":', '"compilerInputChecks":[],"compilerInputChecks":'),
            ('"activity":"' + fixture.OWNERS[0] + '"',
             '"activity":"wrong","activity":"' + fixture.OWNERS[0] + '"'),
            ('"standardInput":"Leo 2","outputs":',
             '"standardInput":"wrong","standardInput":"Leo 2","outputs":'),
            ('"outputs":["Leo: 5","Leo: 5\\n"]',
             '"outputs":["wrong"],"outputs":["Leo: 5","Leo: 5\\n"]'),
        ):
            self.assertIn(old, raw)
            with self.assertRaises((state.LayoutError, ValueError)):
                state.parse_json(raw.replace(old, new, 1))

    def test_authentic_u2l9_preserves_all_editors_history_and_stable_addresses(self):
        before, after = fixture.published_pair()
        self.assertEqual(state.validate(before), state.validate(after))
        for key in before:
            self.assertEqual(before[key], after[key], key)
        blocks = state._blocks(before)
        self.assertEqual([2, 4], [len(blocks[owner]["outputCheck"]["cases"])
                                 for owner in fixture.OWNERS])
        self.assertEqual(fixture.declarations(), after["compilerInputChecks"])
        self.assertEqual("14cbc928ef6925ced0d72e4a1e453765575164369ee0bd36bcfa80bf9d930a54",
                         hashlib.sha256(blocks["block_0007_0001"]["code"].encode()).hexdigest())
        self.assertEqual("3a8116f693d93843a03cde6ec26126e31cc95cf4e866d6e38f5b9511d2830425",
                         hashlib.sha256(blocks["block_0010_0001"]["code"].encode()).hexdigest())
        state.validate_transition(before, after)

    def test_authored_input_contract_is_append_only(self):
        before, after = fixture.published_pair()
        for change in (lambda doc: doc.pop("compilerInputChecks"),
                       lambda doc: doc.update(compilerInputChecks=[]),
                       lambda doc: doc["compilerInputChecks"].reverse(),
                       lambda doc: doc["compilerInputChecks"][0]["probes"].reverse(),
                       lambda doc: doc["compilerInputChecks"][0]["probes"][0].update(standardInput="Leo 3"),
                       lambda doc: doc["compilerInputChecks"][0]["probes"][0].update(outputs=["Leo: 6"])):
            altered = deepcopy(after)
            change(altered)
            with self.assertRaises(state.LayoutError):
                state.validate_transition(after, altered)
        partial = deepcopy(before)
        partial["compilerInputChecks"] = fixture.declarations()[:1]
        state.validate_transition(before, partial)
        state.validate_transition(partial, after)

    def test_export_is_exact_repeatable_and_refuses_conflicting_files(self):
        authentic = fixture.published_bytes()
        self.assertEqual(fixture.PUBLISHED_SHA256, hashlib.sha256(authentic).hexdigest())
        with self.assertRaises(ValueError):
            fixture.verify_published_bytes(authentic + b" ")
        with tempfile.TemporaryDirectory(prefix="lgaed-input-qa-") as temp:
            output = Path(temp)
            manifest = fixture.export(output)
            self.assertEqual(authentic, (output / "before.json").read_bytes())
            contents = json.loads(manifest.read_text(encoding="utf-8"))
            self.assertEqual(list(fixture.OWNERS), contents["activities"])
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
