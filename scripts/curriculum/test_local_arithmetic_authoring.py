"""Explicit arithmetic guards preserve old local-int contracts and provenance."""
from copy import deepcopy
import hashlib
from pathlib import Path
import subprocess
import tempfile
import unittest
from unittest import mock

import short_circuit_fixture_source as fixture
import stable_state_layout as state


def declaration_fixture():
    return {
        "stateLayout": {"version": 2, "locale": "es",
                        "current": [{"blocks": [fixture.ACTIVITY]}]},
        "pages": [{"blocks": [{
            "type": "cpp_compiler", "language": "cpp",
            "initialCode": "#include <iostream>\nint main(){int total=28;int capacidad=6;}\n",
            "outputCheck": {"cases": [{"standardInput": "",
                "outputs": ["0\nGrupos: 4", "0\nGrupos: 4\n"]}]},
        }]}],
        "compilerLocalIntChecks": [{"activity": fixture.ACTIVITY,
                                    "probes": deepcopy(list(fixture.ORIGINAL_PROBES))}],
    }


def observations(document):
    return document["compilerLocalIntChecks"][0]["probes"]


class LocalArithmeticSchemaTests(unittest.TestCase):
    def valid(self, document):
        state._validate_compiler_local_int_checks(document)

    def rejected(self, document):
        with self.assertRaises(state.LayoutError):
            self.valid(document)

    def guarded(self):
        document = declaration_fixture()
        observations(document).extend(fixture.guarded_probes())
        return document

    def test_absent_flag_preserves_exact_legacy_contract_without_normalization(self):
        original = declaration_fixture()
        preserved = deepcopy(original)
        self.valid(original)
        self.assertEqual(original, preserved)
        self.assertTrue(state._preserves_compiler_local_int_checks(original, preserved))
        self.assertFalse(any("forbidArithmetic" in probe for probe in observations(original)))

    def test_zero_and_negative_guards_are_explicit_initialized_integer_observations(self):
        document = self.guarded()
        preserved = deepcopy(document)
        self.valid(document)
        self.assertEqual(document, preserved)
        self.assertEqual([p["replacement"] for p in observations(document)[3:]], [0, -2])
        self.assertTrue(all(p["forbidArithmetic"] is True for p in observations(document)[3:]))
        for value in (-(2 ** 31), -2, 0, 2 ** 31 - 1):
            item = declaration_fixture()
            probe = fixture.guarded_probes()[0]
            probe.update(replacement=value, valueType="int")
            observations(item).append(probe)
            self.valid(item)

    def test_guard_presence_accepts_only_actual_true(self):
        for value in (False, None, 0, 1, 0.0, 1.0, "true", [], {}):
            with self.subTest(value=value):
                document = self.guarded()
                observations(document)[3]["forbidArithmetic"] = value
                self.rejected(document)
        document = declaration_fixture()
        document["compilerLocalIntChecks"][0]["forbidArithmetic"] = True
        self.rejected(document)

    def test_guard_cannot_hide_an_update_joint_replacement_or_noninteger_type(self):
        for field, value in (
                ("afterUpdate", True), ("afterUpdate", False),
                ("expectedIncrease", 3), ("expectedIncrease", 0),
                ("additionalReplacements", []),
                ("additionalReplacements", [{"variable": "total", "replacement": 30}]),
                ("valueType", "char"), ("valueType", "bool"),
                ("valueType", None), ("valueType", "integer"),
                ("forbid_arithmetic", True)):
            with self.subTest(field=field, value=value):
                document = self.guarded()
                observations(document)[3][field] = value
                self.rejected(document)
        for value in (True, False, 0.0, -2.0, "0", None, -(2 ** 31) - 1, 2 ** 31):
            with self.subTest(replacement=value):
                document = self.guarded()
                observations(document)[3]["replacement"] = value
                self.rejected(document)

    def test_duplicates_cannot_be_disguised_by_guard_flag_type_spelling_or_output(self):
        for change in ({}, {"valueType": "int"}, {"outputs": ["another private output"]},
                       {"forbidArithmetic": None}):
            document = self.guarded()
            duplicate = deepcopy(observations(document)[3])
            if change == {"forbidArithmetic": None}:
                duplicate.pop("forbidArithmetic")
            else:
                duplicate.update(change)
            observations(document).append(duplicate)
            with self.subTest(change=change):
                self.rejected(document)
        document = declaration_fixture()
        duplicate = deepcopy(observations(document)[2])
        duplicate["forbidArithmetic"] = True
        observations(document).append(duplicate)
        self.rejected(document)

    def test_guard_keeps_existing_output_and_observation_bounds(self):
        for outputs in (["0\nGrupos: 4"], ["0\nGrupos: 4\r\n"], [], ["\0"],
                        ["private\n", "private\r\n"], ["x" * 4097]):
            document = self.guarded()
            observations(document)[3]["outputs"] = outputs
            with self.subTest(outputs=str(outputs)[:60]):
                self.rejected(document)
        document = declaration_fixture()
        for value in range(-8, -2):
            probe = fixture.guarded_probes()[0]
            probe["replacement"] = value
            observations(document).append(probe)
        self.assertEqual(len(observations(document)), 9)
        self.rejected(document)

    def test_scalar_owner_extension_requires_every_new_observation_to_opt_in(self):
        original, extended = declaration_fixture(), self.guarded()
        self.assertTrue(state._preserves_compiler_local_int_checks(original, extended))
        self.assertFalse(state._preserves_compiler_local_int_checks(extended, original))
        for index in (3, 4):
            for value in (None, False, 1):
                document = deepcopy(extended)
                if value is None:
                    observations(document)[index].pop("forbidArithmetic")
                else:
                    observations(document)[index]["forbidArithmetic"] = value
                self.assertFalse(state._preserves_compiler_local_int_checks(original, document))
        later = deepcopy(extended)
        observations(later).append({"variable": "total", "replacement": 36,
                                   "outputs": ["1\nGrupos: 6"]})
        self.valid(later)
        self.assertFalse(state._preserves_compiler_local_int_checks(extended, later))

    def test_old_prefix_and_owner_cannot_be_rewritten_even_when_new_guards_exist(self):
        original, extended = declaration_fixture(), self.guarded()
        for field, value in (("replacement", 32), ("outputs", ["other result"]),
                             ("variable", "otra"), ("forbidArithmetic", True),
                             ("valueType", "int")):
            altered = deepcopy(extended)
            observations(altered)[0][field] = value
            self.assertFalse(state._preserves_compiler_local_int_checks(original, altered))
        reordered = deepcopy(extended)
        observations(reordered)[:3] = observations(reordered)[:3][::-1]
        self.assertFalse(state._preserves_compiler_local_int_checks(original, reordered))
        changed_owner = deepcopy(extended)
        changed_owner["compilerLocalIntChecks"][0]["activity"] = "another_editor"
        self.assertFalse(state._preserves_compiler_local_int_checks(original, changed_owner))

    def test_existing_joint_epoch_remains_compatible_without_an_implicit_guard(self):
        original = declaration_fixture()
        extended = deepcopy(original)
        observations(extended).append({"variable": "capacidad", "replacement": 3,
            "additionalReplacements": [{"variable": "total", "replacement": 18}],
            "outputs": ["1\nGrupos: 6"]})
        self.valid(extended)
        self.assertTrue(state._preserves_compiler_local_int_checks(original, extended))
        later = deepcopy(extended)
        observations(later).append({"variable": "total", "replacement": 36,
                                   "outputs": ["1\nGrupos: 6"]})
        self.valid(later)
        self.assertTrue(state._preserves_compiler_local_int_checks(extended, later))
        self.assertFalse(any("forbidArithmetic" in p for p in observations(later)))


class ShortCircuitFixtureTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.payloads = fixture.export_payloads()
        cls.manifest = state.parse_json(cls.payloads["manifest.json"].decode("utf-8"))
        cls.lesson = cls.manifest["lessons"][0]

    def document(self, version):
        return state.parse_json(self.payloads[f"u3l6/{version}.json"].decode("utf-8"))

    def test_complete_original_requires_both_hashes_and_pinned_git_without_fallback(self):
        original = self.payloads["u3l6/before.json"]
        self.assertEqual(fixture.PUBLISHED_COMMIT, "08a11877a2d2c0bf96a846660e3baea69e53f758")
        self.assertEqual(hashlib.sha256(original).hexdigest(), fixture.PUBLISHED_SHA256)
        self.assertEqual(hashlib.sha1(b"blob " + str(len(original)).encode("ascii") +
                                     b"\0" + original).hexdigest(), fixture.PUBLISHED_BLOB_SHA)
        self.assertEqual(fixture.verify_published_bytes(original), original)
        for invalid in (original + b" ", original[:-1], self.payloads["u3l6/after.json"]):
            with self.assertRaises(ValueError):
                fixture.verify_published_bytes(invalid)
        for field in ("PUBLISHED_SHA256", "PUBLISHED_BLOB_SHA"):
            with mock.patch.object(fixture, field, "0" * len(getattr(fixture, field))):
                with self.assertRaises(ValueError):
                    fixture.verify_published_bytes(original)
        with mock.patch.object(fixture.subprocess, "check_output", return_value=original) as read:
            self.assertEqual(fixture.published_bytes(), original)
        args, kwargs = read.call_args
        self.assertEqual(args[0], ["git", "--no-optional-locks", "-c",
            f"safe.directory={fixture.ROOT}", "show",
            f"{fixture.PUBLISHED_COMMIT}:{fixture.PUBLISHED_PATH}"])
        self.assertEqual(kwargs["cwd"], fixture.ROOT)
        for key, value in (("GIT_NO_LAZY_FETCH", "1"), ("GIT_NO_REPLACE_OBJECTS", "1"),
                           ("GIT_TERMINAL_PROMPT", "0")):
            self.assertEqual(kwargs["env"][key], value)
        with mock.patch.object(fixture.subprocess, "check_output",
                               side_effect=subprocess.CalledProcessError(128, ["git", "show"])):
            with self.assertRaisesRegex(RuntimeError, "no fetch or current-asset fallback"):
                fixture.published_bytes()

    def test_after_only_appends_the_two_explicit_guards_to_the_exact_three_probe_prefix(self):
        before, after = self.document("before"), self.document("after")
        self.assertEqual(observations(before), list(fixture.ORIGINAL_PROBES))
        self.assertEqual(observations(after), list(fixture.ORIGINAL_PROBES) + fixture.guarded_probes())
        restored = deepcopy(after)
        del observations(restored)[3:]
        self.assertEqual(restored, before)
        self.assertEqual(state._blocks(before), state._blocks(after))
        self.assertEqual(before["stateLayout"], after["stateLayout"])
        state.validate_transition(before, after)
        only_zero = deepcopy(after)
        observations(only_zero).pop()
        state.validate_transition(before, only_zero)
        state.validate_transition(only_zero, after)
        for changed in (before, only_zero):
            with self.assertRaises(state.LayoutError):
                state.validate_transition(after, changed)

    def test_real_transition_rejects_old_probe_or_public_answer_mutation_and_unflagged_suffix(self):
        before, after = self.document("before"), self.document("after")
        bad = []
        for field, value in (("replacement", 31), ("forbidArithmetic", True),
                             ("outputs", ["rewritten hidden result"])):
            item = deepcopy(after)
            observations(item)[0][field] = value
            bad.append(item)
        item = deepcopy(after)
        state._blocks(item)[fixture.ACTIVITY]["outputCheck"]["cases"][0]["outputs"] = ["forged public result"]
        bad.append(item)
        item = deepcopy(after)
        observations(item)[-1].pop("forbidArithmetic")
        bad.append(item)
        item = deepcopy(after)
        observations(item)[-1]["forbidArithmetic"] = 1
        bad.append(item)
        item = deepcopy(after)
        observations(item).append({"variable": "total", "replacement": 36,
                                  "outputs": ["1\nGrupos: 6"]})
        bad.append(item)
        for number, item in enumerate(bad):
            with self.subTest(mutation=number):
                with self.assertRaises(state.LayoutError):
                    state.validate_transition(before, item)

    def test_manifest_keeps_both_authentic_references_and_ten_explicit_programs(self):
        self.assertEqual(set(self.manifest), {"version", "kind", "publishedCommit", "lessons"})
        self.assertEqual(self.manifest["kind"], "guarded-local-arithmetic")
        self.assertEqual(self.manifest["version"], 1)
        self.assertEqual(self.manifest["publishedCommit"], fixture.PUBLISHED_COMMIT)
        self.assertEqual(len(self.manifest["lessons"]), 1)
        self.assertEqual(set(self.payloads), {"manifest.json", "u3l6/before.json", "u3l6/after.json"})
        blocks = state._blocks(self.document("before"))
        for version in ("before", "after"):
            self.assertEqual(self.lesson[version], f"u3l6/{version}.json")
            self.assertEqual(self.lesson[version + "Sha256"],
                             hashlib.sha256(self.payloads[self.lesson[version]]).hexdigest())
        self.assertEqual({item["activity"] for item in self.lesson["solutions"]}, set(fixture.REFERENCES))
        for solution in self.lesson["solutions"]:
            self.assertEqual(solution["source"], blocks[fixture.REFERENCES[solution["activity"]]]["code"])
            self.assertEqual(solution["sourceSha256"], fixture.REFERENCE_SHA256[solution["activity"]])
        plan, = self.lesson["plans"]
        self.assertEqual((plan["activity"], plan["reference"], plan["originalProbeCount"]),
                         (fixture.ACTIVITY, fixture.REFERENCE, 3))
        programs = {item["label"]: item for item in plan["sources"]}
        self.assertEqual(len(plan["sources"]), 10)
        self.assertEqual({label for label, item in programs.items() if item["accepted"]},
                         {"reference", "guarded_if", "guarded_ternary", "negated_or", "keyword_and", "primitive_casts"})
        self.assertEqual({label for label, item in programs.items() if "guardExit" in item},
                         {"reversed_and", "eager_and", "precomputed"})
        self.assertFalse(programs["initial"]["accepted"])
        self.assertNotIn("guardExit", programs["initial"])
        self.assertEqual(programs["initial"]["source"], blocks[fixture.ACTIVITY]["initialCode"])
        self.assertEqual(programs["reference"]["source"], blocks[fixture.REFERENCE]["code"])
        self.assertIn("static_cast<bool>(capacidad > 0) && "
                      "(int(total) % static_cast<int>(capacidad) == 0)",
                      programs["primitive_casts"]["source"])
        self.assertEqual(plan["fixedSource"], programs["reversed_and"]["source"])
        self.assertEqual(plan["fixedSourceSha256"], programs["reversed_and"]["sourceSha256"])
        for program in programs.values():
            self.assertEqual(program["sourceSha256"], hashlib.sha256(program["source"].encode("utf-8")).hexdigest())
            self.assertIs(type(program["accepted"]), bool)
            if "guardExit" in program:
                self.assertEqual(program["guardExit"], 91)
        self.assertEqual(plan["sources"], fixture.source_variants(
            blocks[fixture.REFERENCE]["code"], blocks[fixture.ACTIVITY]["initialCode"]))

    def test_program_variants_cannot_be_built_from_changed_or_duplicated_sources(self):
        blocks = state._blocks(self.document("before"))
        reference, initial = blocks[fixture.REFERENCE]["code"], blocks[fixture.ACTIVITY]["initialCode"]
        for wrong in (reference + reference, reference + "// changed\n", "int main() {}\n"):
            with self.assertRaises(ValueError):
                fixture.source_variants(wrong, initial)
        with self.assertRaises(ValueError):
            fixture.source_variants(reference, initial + "// changed\n")
        with self.assertRaises(ValueError):
            fixture.replace_once("two two", "two", "three")

    def test_export_is_idempotent_and_checks_all_conflicts_before_writing(self):
        with tempfile.TemporaryDirectory() as directory:
            output = Path(directory) / "guarded"
            with mock.patch.object(fixture, "export_payloads", return_value=self.payloads):
                self.assertEqual(fixture.export(output), output.resolve() / "manifest.json")
                original = {name: ((output / name).read_bytes(), (output / name).stat().st_mtime_ns)
                            for name in self.payloads}
                self.assertEqual(fixture.export(output), output.resolve() / "manifest.json")
            for name, (payload, modified) in original.items():
                self.assertEqual(payload, self.payloads[name])
                self.assertEqual((output / name).read_bytes(), payload)
                self.assertEqual((output / name).stat().st_mtime_ns, modified)
        with tempfile.TemporaryDirectory() as directory:
            output = Path(directory) / "guarded"
            conflict = output / "manifest.json"
            output.mkdir()
            conflict.write_bytes(b"preserve unrelated existing bytes")
            with mock.patch.object(fixture, "export_payloads", return_value=self.payloads):
                with self.assertRaises(FileExistsError):
                    fixture.export(output)
            self.assertEqual(list(output.iterdir()), [conflict])
            self.assertEqual(conflict.read_bytes(), b"preserve unrelated existing bytes")

    def test_default_historical_payloads_remain_byte_for_byte_equivalent(self):
        expected = {
            "u3l6/before.json": "9d7ca6b5d82b2dac1809ab01b8b308f9e81027c4c4c6ce796502b42cda7ca8ea",
            "u3l6/after.json": "4957a5aa3692d9548a1ed724ec792bf62bfc4ffa9ce8443ed2fda1ae0d064159",
            "manifest.json": "31796a2be9055fb5141a19758d55cabcb6f448ac699ec67fa421f1bf4f171c4a",
        }
        self.assertEqual({name: hashlib.sha256(payload).hexdigest()
                          for name, payload in self.payloads.items()}, expected)


class ActiveShortCircuitFixtureTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.payloads = fixture.active_payloads()
        cls.manifest = state.parse_json(cls.payloads["manifest.json"].decode("utf-8"))
        cls.lesson = cls.manifest["lessons"][0]

    def document(self, version):
        return state.parse_json(self.payloads[f"u3l6/{version}.json"].decode("utf-8"))

    def test_active_source_requires_both_exact_hashes_and_never_falls_back_to_archived_bytes(self):
        original = self.payloads["u3l6/before.json"]
        self.assertEqual(fixture.ACTIVE_COMMIT, "39ba3cb196e196bac99c2d891b488f765679ad6d")
        self.assertEqual(fixture.ACTIVE_SHA256, "5c60bcac3bf8fd75ceb379dcce348553d90dbb27373ac5bffb01beea72a4853e")
        self.assertEqual(fixture.ACTIVE_BLOB_SHA, "064ddec6a1b9915b6f3cf2352268a8cf4f50fbd2")
        self.assertEqual(hashlib.sha256(original).hexdigest(), fixture.ACTIVE_SHA256)
        self.assertEqual(hashlib.sha1(b"blob " + str(len(original)).encode("ascii") +
                                     b"\0" + original).hexdigest(), fixture.ACTIVE_BLOB_SHA)
        self.assertEqual(fixture.verify_published_bytes(original, active=True), original)
        for invalid in (original + b" ", original[:-1], self.payloads["u3l6/after.json"],
                        fixture.published_bytes()):
            with self.assertRaises(ValueError):
                fixture.verify_published_bytes(invalid, active=True)
        for field in ("ACTIVE_SHA256", "ACTIVE_BLOB_SHA"):
            with mock.patch.object(fixture, field, "0" * len(getattr(fixture, field))):
                with self.assertRaises(ValueError):
                    fixture.verify_published_bytes(original, active=True)
        with mock.patch.object(fixture.subprocess, "check_output", return_value=original) as read:
            self.assertEqual(fixture.published_bytes(active=True), original)
        self.assertEqual(read.call_args.args[0], ["git", "--no-optional-locks", "-c",
            f"safe.directory={fixture.ROOT}", "show",
            "39ba3cb196e196bac99c2d891b488f765679ad6d:" +
            "app/src/main/assets/content/es/programming/cpp_fundamentals/" +
            "edition_es_1__expresiones_y_decisiones/lesson_validez_y_cortocircuito.json"])
        self.assertEqual(read.call_args.kwargs["cwd"], fixture.ROOT)
        for key, value in (("GIT_NO_LAZY_FETCH", "1"), ("GIT_NO_REPLACE_OBJECTS", "1"),
                           ("GIT_TERMINAL_PROMPT", "0")):
            self.assertEqual(read.call_args.kwargs["env"][key], value)
        with mock.patch.object(fixture.subprocess, "check_output",
                               side_effect=subprocess.CalledProcessError(128, ["git", "show"])) as read:
            with self.assertRaisesRegex(RuntimeError, "no fetch or current-asset fallback"):
                fixture.published_bytes(active=True)
        read.assert_called_once()

    def test_active_after_preserves_edition_all_blocks_maps_and_the_three_probe_prefix(self):
        before, after = self.document("before"), self.document("after")
        self.assertEqual(before["compilerLocalIntChecks"], [
            {"activity": "block_0003_0002", "probes": list(fixture.ORIGINAL_PROBES)}])
        self.assertEqual(observations(after), list(fixture.ORIGINAL_PROBES) + [
            {"variable": "capacidad", "replacement": 0,
             "outputs": ["0\nCapacidad invalida", "0\nCapacidad invalida\n"], "forbidArithmetic": True},
            {"variable": "capacidad", "replacement": -2,
             "outputs": ["0\nCapacidad invalida", "0\nCapacidad invalida\n"], "forbidArithmetic": True}])
        restored = deepcopy(after)
        del observations(restored)[3:]
        self.assertEqual(restored, before)
        self.assertEqual(before["courseEdition"], {
            "version": 1, "courseId": "cpp_fundamentals", "locale": "es",
            "generation": 1, "id": "cpp_desde_cero_2026"})
        self.assertEqual(before["courseEdition"], after["courseEdition"])
        self.assertEqual(before["pages"], after["pages"])
        self.assertEqual(before["stateLayout"], after["stateLayout"])
        self.assertEqual(state._blocks(before), state._blocks(after))
        state.validate_transition(before, after)
        with self.assertRaises(state.LayoutError):
            state.validate_transition(after, before)
        blocks = state._blocks(before)
        self.assertTrue(before["compilerPuzzlesRequired"])
        self.assertEqual(blocks["block_0003_0002"]["outputCheck"], {"cases": [
            {"standardInput": "", "outputs": ["0\nGrupos: 4", "0\nGrupos: 4\n"]}]})
        self.assertEqual(blocks["block_0006_0002"]["initialStandardInput"], "28 0")
        self.assertEqual(len(blocks["block_0006_0002"]["outputCheck"]["cases"]), 7)

    def test_active_manifest_keeps_current_owners_and_all_ten_existing_programs_unchanged(self):
        self.assertEqual(set(self.manifest), {"version", "kind", "publishedCommit", "lessons"})
        self.assertEqual((self.manifest["version"], self.manifest["kind"]), (1, "active-guarded-arithmetic"))
        self.assertEqual(self.manifest["publishedCommit"], fixture.ACTIVE_COMMIT)
        self.assertEqual(self.lesson["publishedCommit"], fixture.ACTIVE_COMMIT)
        self.assertEqual(len(self.manifest["lessons"]), 1)
        self.assertEqual(set(self.payloads), {"manifest.json", "u3l6/before.json", "u3l6/after.json"})
        references = {"block_0003_0002": "block_0003_0005", "block_0006_0002": "block_0006_0006"}
        digests = {
            "block_0003_0002": "134ba6d6c94ba286a38d7db970af6df951668d3dee629225c977364c9561bbce",
            "block_0006_0002": "cc7b0a8c1bd48f6066cbbc53077ee5e28a2df63c672f16289a13a4b9422e1224"}
        blocks = state._blocks(self.document("before"))
        self.assertEqual({identity for identity, block in blocks.items()
                          if block.get("type") == "cpp_compiler"}, set(references))
        self.assertEqual({item["activity"] for item in self.lesson["solutions"]}, set(references))
        for solution in self.lesson["solutions"]:
            owner = solution["activity"]
            self.assertEqual(solution["source"], blocks[references[owner]]["code"])
            self.assertEqual(solution["sourceSha256"], digests[owner])
        for version in ("before", "after"):
            self.assertEqual(self.lesson[version], f"u3l6/{version}.json")
            self.assertEqual(self.lesson[version + "Sha256"],
                             hashlib.sha256(self.payloads[self.lesson[version]]).hexdigest())
        plan, = self.lesson["plans"]
        self.assertEqual((plan["activity"], plan["reference"], plan["originalProbeCount"]),
                         ("block_0003_0002", "block_0003_0005", 3))
        historical = state.parse_json(fixture.export_payloads()["manifest.json"].decode("utf-8"))
        previous = historical["lessons"][0]["plans"][0]
        for field in ("sources", "fixedSource", "fixedSourceSha256"):
            self.assertEqual(plan[field], previous[field])
        self.assertEqual(len(plan["sources"]), 10)
        self.assertEqual([item["label"] for item in plan["sources"] if item.get("guardExit") == 91],
                         ["reversed_and", "eager_and", "precomputed"])

    def test_active_transition_rejects_old_answer_probe_owner_and_guard_mutations(self):
        before, after = self.document("before"), self.document("after")
        bad = []
        for field, value in (("replacement", 31), ("outputs", ["forged private result"]),
                             ("forbidArithmetic", True)):
            changed = deepcopy(after)
            observations(changed)[0][field] = value
            bad.append(changed)
        changed = deepcopy(after)
        changed["compilerLocalIntChecks"][0]["activity"] = "block_0006_0002"
        bad.append(changed)
        changed = deepcopy(after)
        state._blocks(changed)["block_0003_0002"]["outputCheck"]["cases"][0]["outputs"] = ["forged public result"]
        bad.append(changed)
        for value in (None, False, 1):
            changed = deepcopy(after)
            if value is None:
                observations(changed)[-1].pop("forbidArithmetic")
            else:
                observations(changed)[-1]["forbidArithmetic"] = value
            bad.append(changed)
        for number, changed in enumerate(bad):
            with self.subTest(mutation=number):
                with self.assertRaises(state.LayoutError):
                    state.validate_transition(before, changed)

    def test_active_export_preserves_bytes_mtimes_and_rejects_mixing_cohorts(self):
        with tempfile.TemporaryDirectory() as directory:
            output = Path(directory) / "active-guarded"
            with mock.patch.object(fixture, "active_payloads", return_value=self.payloads):
                self.assertEqual(fixture.export(output, active=True), output.resolve() / "manifest.json")
                original = {name: ((output / name).read_bytes(), (output / name).stat().st_mtime_ns)
                            for name in self.payloads}
                fixture.export(output, active=True)
            with self.assertRaises(FileExistsError):
                fixture.export(output)
            for name, (payload, modified) in original.items():
                self.assertEqual((output / name).read_bytes(), payload)
                self.assertEqual((output / name).stat().st_mtime_ns, modified)
        with tempfile.TemporaryDirectory() as directory:
            output = Path(directory) / "active-guarded"
            with mock.patch.object(fixture.subprocess, "check_output",
                                   return_value=self.payloads["u3l6/before.json"] + b" ") as read:
                with self.assertRaises(ValueError):
                    fixture.export(output, active=True)
            read.assert_called_once()
            self.assertFalse(output.exists())


if __name__ == "__main__":
    unittest.main()
