"""Authenticate complete input-decision fixtures and their preserved contracts."""
from copy import deepcopy
import hashlib
import json
import os
from pathlib import Path
import subprocess
import tempfile
import unittest
from unittest import mock

import input_decision_fixture_source as fixture
import stable_state_layout as state


class InputDecisionAuthoringTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.payloads = fixture.export_payloads()
        cls.manifest = state.parse_json(cls.payloads["manifest.json"].decode("utf-8"))
        cls.lessons = {lesson["id"]: lesson for lesson in cls.manifest["lessons"]}

    def document(self, name, version):
        return state.parse_json(self.payloads[f"{name}/{version}.json"].decode("utf-8"))

    def test_full_publications_are_pinned_without_fetch_or_live_asset_fallback(self):
        self.assertEqual(fixture.PUBLISHED_COMMIT,
                         "08a11877a2d2c0bf96a846660e3baea69e53f758")
        self.assertEqual(tuple(fixture.PUBLICATIONS),
                         ("u3l3", "u3l4", "u3l5", "u3l6", "u3l7", "u3l8", "u4l1", "u4l2"))
        for name, publication in fixture.PUBLICATIONS.items():
            with self.subTest(lesson=name):
                original = self.payloads[f"{name}/before.json"]
                self.assertEqual(hashlib.sha256(original).hexdigest(), publication["sha256"])
                self.assertEqual(hashlib.sha1(b"blob " + str(len(original)).encode("ascii") +
                                             b"\0" + original).hexdigest(), publication["blobSha"])
                self.assertEqual(fixture.verify_published_bytes(name, original), original)
                for invalid in (original + b" ", original[:-1], self.payloads[f"{name}/after.json"]):
                    with self.assertRaises(ValueError):
                        fixture.verify_published_bytes(name, invalid)
                with mock.patch.object(fixture.subprocess, "check_output", return_value=original) as read:
                    self.assertEqual(fixture.published_bytes(name), original)
                args, kwargs = read.call_args
                self.assertEqual(args[0], ["git", "--no-optional-locks", "-c",
                    f"safe.directory={fixture.ROOT}", "show",
                    f"{fixture.PUBLISHED_COMMIT}:{publication['path']}"])
                self.assertEqual(kwargs["cwd"], fixture.ROOT)
                self.assertEqual(kwargs["env"]["GIT_NO_LAZY_FETCH"], "1")
                self.assertEqual(kwargs["env"]["GIT_NO_REPLACE_OBJECTS"], "1")
                self.assertEqual(kwargs["env"]["GIT_TERMINAL_PROMPT"], "0")
        with mock.patch.object(fixture.subprocess, "check_output",
                               side_effect=subprocess.CalledProcessError(128, ["git", "show"])):
            with self.assertRaisesRegex(RuntimeError, "no fetch or current-asset fallback"):
                fixture.published_bytes("u3l3")

    def test_after_adds_only_input_root_and_preserves_all_historical_fields(self):
        for name, publication in fixture.PUBLICATIONS.items():
            with self.subTest(lesson=name):
                before, after = self.document(name, "before"), self.document(name, "after")
                self.assertNotIn("compilerInputChecks", before)
                self.assertEqual(set(after), set(before) | {"compilerInputChecks"})
                self.assertEqual(after["compilerInputChecks"], fixture.declarations(name))
                preserved = deepcopy(after)
                del preserved["compilerInputChecks"]
                self.assertEqual(preserved, before)
                self.assertEqual(state._blocks(before), state._blocks(after))
                self.assertEqual(before["stateLayout"], after["stateLayout"])
                self.assertEqual(before["compilerLocalIntChecks"], after["compilerLocalIntChecks"])
                self.assertTrue(before["compilerPuzzlesRequired"])
                state.validate_transition(before, after)
                with self.assertRaises(state.LayoutError):
                    state.validate_transition(after, before)
                rewritten = deepcopy(after)
                rewritten["compilerInputChecks"][0]["probes"][0]["outputs"] = ["rewritten result"]
                with self.assertRaises(state.LayoutError):
                    state.validate_transition(after, rewritten)

    def test_exact_eight_plans_keep_53_public_cases_and_select_40_private_cases(self):
        expected_inputs = {
            "u3l3": ("19 20", "21 20", "20 18"),
            "u3l4": ("4 4", "5 4", "3 3"),
            "u3l5": ("0 0 0", "0 0 1", "0 1 0", "0 1 1", "1 0 1", "1 1 0", "1 1 1"),
            "u3l6": ("28 -1", "28 4", "28 6", "0 4", "1 4", "120 12"),
            "u3l7": ("6 1", "0 1", "13 1", "1 1", "4 1", "5 1", "12 1", "6 0"),
            "u3l8": ("14 4 1", "14 4 0", "12 4 1", "12 4 0", "30 6 1", "1 4 1", "1 4 0"),
            "u4l1": ("1", "4", "5"),
            "u4l2": ("5", "2", "1"),
        }
        expected_public_counts = dict(zip(expected_inputs, (4, 4, 8, 7, 9, 13, 4, 4)))
        public_total = private_total = 0
        for name, lesson in self.lessons.items():
            with self.subTest(lesson=name):
                self.assertEqual(len(lesson["plans"]), 1)
                plan = lesson["plans"][0]
                before = self.document(name, "before")
                owner = state._blocks(before)[plan["activity"]]
                public = owner["outputCheck"]["cases"]
                check = self.document(name, "after")["compilerInputChecks"][0]
                probes = check["probes"]
                self.assertEqual(owner["initialStandardInput"], public[0]["standardInput"])
                self.assertEqual(owner["outputCheck"]["inputComparison"], "tokens")
                self.assertEqual(plan["inputComparison"], "tokens")
                self.assertEqual(plan["publicCases"], public)
                serialized = (json.dumps(public, ensure_ascii=False, indent=2,
                                         allow_nan=False) + "\n").encode("utf-8")
                self.assertEqual(plan["publicCasesSha256"], hashlib.sha256(serialized).hexdigest())
                self.assertEqual(len(public), expected_public_counts[name])
                self.assertEqual(tuple(probe["standardInput"] for probe in probes), expected_inputs[name])
                self.assertGreaterEqual(len(probes), 1)
                self.assertLessEqual(len(probes), 8)
                for probe in probes:
                    self.assertIn(probe, public[1:])
                    self.assertNotEqual(probe["standardInput"], public[0]["standardInput"])
                for case in public:
                    self.assertEqual(set(case), {"standardInput", "outputs"})
                    self.assertEqual(len(case["outputs"]), 2)
                    self.assertEqual(case["outputs"][1], case["outputs"][0] + "\n")
                public_total += len(public)
                private_total += len(probes)
        self.assertEqual((public_total, private_total), (53, 40))
        self.assertEqual(len(self.document("u3l8", "before")["pages"]), 6)

    def test_only_three_plans_opt_in_and_they_still_require_a_disjoint_observation(self):
        opted_in = set()
        for name in self.lessons:
            after = self.document(name, "after")
            check = after["compilerInputChecks"][0]
            first = state._blocks(after)[check["activity"]]["outputCheck"]["cases"][0]
            first_outputs = set(first["outputs"])
            same = [probe for probe in check["probes"] if first_outputs & set(probe["outputs"])]
            distinct = [probe for probe in check["probes"] if not first_outputs & set(probe["outputs"])]
            self.assertTrue(distinct)
            if name not in {"u3l5", "u3l6", "u3l7"}:
                self.assertNotIn("allowSameOutput", check)
                self.assertFalse(same)
                continue
            opted_in.add(name)
            self.assertIs(check["allowSameOutput"], True)
            self.assertTrue(same)
            omitted = deepcopy(after)
            del omitted["compilerInputChecks"][0]["allowSameOutput"]
            with self.assertRaises(state.LayoutError):
                state._validate_compiler_input_checks(omitted)
            for invalid in (False, None, 0, 1, "true"):
                malformed = deepcopy(after)
                malformed["compilerInputChecks"][0]["allowSameOutput"] = invalid
                with self.assertRaises(state.LayoutError):
                    state._validate_compiler_input_checks(malformed)
            invariant_only = deepcopy(after)
            invariant_only["compilerInputChecks"][0]["probes"] = deepcopy(same)
            with self.assertRaises(state.LayoutError):
                state._validate_compiler_input_checks(invariant_only)
            mixed_alternatives = deepcopy(invariant_only)
            ambiguous = deepcopy(distinct[0])
            ambiguous["outputs"] = [first["outputs"][0], distinct[0]["outputs"][0]]
            mixed_alternatives["compilerInputChecks"][0]["probes"].append(ambiguous)
            with self.assertRaises(state.LayoutError):
                state._validate_compiler_input_checks(mixed_alternatives)
        self.assertEqual(opted_in, {"u3l5", "u3l6", "u3l7"})

    def test_manifest_preserves_two_authentic_solutions_and_correct_u3l8_references(self):
        self.assertEqual(set(self.manifest), {"version", "kind", "publishedCommit", "lessons"})
        self.assertEqual(self.manifest["kind"], "input-decision")
        self.assertEqual(self.manifest["version"], 1)
        self.assertEqual(self.manifest["publishedCommit"], fixture.PUBLISHED_COMMIT)
        self.assertEqual(len(self.payloads), 17)
        solutions = 0
        for name, lesson in self.lessons.items():
            with self.subTest(lesson=name):
                blocks = state._blocks(self.document(name, "before"))
                compilers = {identity for identity, block in blocks.items()
                             if block.get("type") == "cpp_compiler"}
                self.assertEqual(len(compilers), 2)
                self.assertEqual({item["activity"] for item in lesson["solutions"]}, compilers)
                for version in ("before", "after"):
                    self.assertEqual(lesson[version], f"{name}/{version}.json")
                    self.assertEqual(lesson[version + "Sha256"],
                                     hashlib.sha256(self.payloads[lesson[version]]).hexdigest())
                for solution in lesson["solutions"]:
                    reference = fixture.PUBLICATIONS[name]["references"][solution["activity"]]
                    self.assertEqual(solution["source"], blocks[reference]["code"])
                    self.assertEqual(solution["sourceSha256"],
                                     hashlib.sha256(solution["source"].encode("utf-8")).hexdigest())
                    solutions += 1
                plan = lesson["plans"][0]
                self.assertEqual(plan["activity"], fixture.PUBLICATIONS[name]["activity"])
                first = blocks[plan["activity"]]["outputCheck"]["cases"][0]
                self.assertEqual(plan["fixedSource"], fixture.fixed_output_source(first["outputs"][0]))
                self.assertNotIn("std::cin", plan["fixedSource"])
                self.assertEqual(plan["fixedSource"].count("std::cout"), 1)
                self.assertEqual(plan["fixedSourceSha256"],
                                 hashlib.sha256(plan["fixedSource"].encode("utf-8")).hexdigest())
        self.assertEqual(solutions, 16)
        self.assertEqual(fixture.PUBLICATIONS["u3l8"]["references"], {
            "block_0002_0002": "p1u3l8_ejemplo_original",
            "p1u3l8_cambiar_minimo": "p1u3l8_ejemplo_minimo"})
        self.assertEqual(self.lessons["u3l8"]["plans"][0]["reference"], "p1u3l8_ejemplo_minimo")

    def test_five_exact_negatives_target_equal_output_cases_without_unsafe_zero_division(self):
        expected_failures = {
            "u3l5": {"open_only": ["0 0 1"],
                     "invitation_ignores_closure": ["0 1 0", "1 1 0"]},
            "u3l6": {"nonzero_instead_of_positive": ["28 -1"]},
            "u3l7": {"no_upper_bound": ["13 1"], "zero_allowed_with_permission": ["0 1"]},
        }
        total = 0
        for name, lesson in self.lessons.items():
            plan = lesson["plans"][0]
            blocks = state._blocks(self.document(name, "before"))
            reference = blocks[plan["reference"]]["code"]
            negatives = plan["invariantCounterexamples"]
            self.assertEqual({item["label"]: item["failedInputs"] for item in negatives},
                             expected_failures.get(name, {}))
            self.assertEqual(negatives, fixture.invariant_counterexamples(name, reference))
            for negative in negatives:
                self.assertEqual(set(negative), {"label", "source", "sourceSha256", "failedInputs"})
                self.assertNotEqual(negative["source"], reference)
                self.assertEqual(negative["sourceSha256"],
                                 hashlib.sha256(negative["source"].encode("utf-8")).hexdigest())
                total += 1
            if negatives:
                for corrupted in (reference + reference, "int main() {}\n", reference + "// changed\n"):
                    with self.assertRaises(ValueError):
                        fixture.invariant_counterexamples(name, corrupted)
        self.assertEqual(total, 5)
        remainder = self.lessons["u3l6"]["plans"][0]["invariantCounterexamples"][0]["source"]
        self.assertEqual(remainder.count("capacidad != 0"), 2)
        self.assertNotIn("capacidad > 0", remainder)


    def test_active_edition_loop_input_contracts(self):
        # Exercise the actual new-edition identities without editing curriculum
        # or relying on whichever version happens to be checked out.
        revision = "2fcfad809e6a854dc35f4ce6c19fc1f1ab3e9c5b"
        active_owners = {
            "u4l1": ("block_0006_0002", "block_0005_0002",
                     "13b99776f21822f97d25ecb5682bbc2ed71dd13d"),
            "u4l2": ("block_0006_0005", "block_0005_0003",
                     "0d89c13d179f0b9aa48006e3e873841f5e6822f8"),
        }
        for name, (identity, repair, blob_sha) in active_owners.items():
            with self.subTest(lesson=name):
                path = fixture.PUBLICATIONS[name]["path"].replace(
                    "/repeticion_y_terminacion/", "/edition_es_1__repeticion_y_terminacion/")
                raw = subprocess.check_output(
                    ["git", "--no-optional-locks", "-c", f"safe.directory={fixture.ROOT}",
                     "show", f"{revision}:{path}"], cwd=fixture.ROOT,
                    env=dict(os.environ, GIT_NO_LAZY_FETCH="1",
                             GIT_NO_REPLACE_OBJECTS="1", GIT_TERMINAL_PROMPT="0"))
                self.assertEqual(hashlib.sha1(b"blob " + str(len(raw)).encode("ascii") +
                                             b"\0" + raw).hexdigest(), blob_sha)
                before = state.parse_json(raw.decode("utf-8"))
                self.assertEqual(before["courseEdition"]["generation"], 1)
                self.assertNotIn("compilerInputChecks", before)
                owner = state._blocks(before)[identity]
                self.assertEqual(owner["type"], "cpp_compiler")
                self.assertEqual(owner["outputCheck"]["cases"], fixture.public_cases(name))
                self.assertTrue(before["compilerPuzzlesRequired"])
                after = deepcopy(before)
                check = deepcopy(fixture.declarations(name)[0])
                check["activity"] = identity
                after["compilerInputChecks"] = [check]
                state.validate_transition(before, after)
                self.assertEqual(state._blocks(before), state._blocks(after))
                self.assertEqual(before["stateLayout"], after["stateLayout"])
                self.assertEqual(before["referenceAfter"], after["referenceAfter"])
                self.assertEqual(before["compilerLocalIntChecks"], after["compilerLocalIntChecks"])
                self.assertEqual({item["activity"] for item in before["compilerLocalIntChecks"]},
                                 {repair})
                self.assertEqual({item["activity"] for item in before["referenceAfter"]},
                                 {repair, identity})
                self.assertEqual(check["probes"], owner["outputCheck"]["cases"][1:])
                self.assertEqual(owner["initialStandardInput"],
                                 owner["outputCheck"]["cases"][0]["standardInput"])
                for probe in check["probes"]:
                    self.assertTrue(set(probe["outputs"]).isdisjoint(
                        owner["outputCheck"]["cases"][0]["outputs"]))
                rewritten = deepcopy(after)
                rewritten["compilerInputChecks"][0]["probes"][0]["outputs"] = ["rewritten result"]
                with self.assertRaises(state.LayoutError):
                    state.validate_transition(after, rewritten)
                with self.assertRaises(state.LayoutError):
                    state.validate_transition(after, before)
                del after["compilerInputChecks"]
                self.assertEqual(after, before)


    def test_export_is_idempotent_and_preserves_exact_bytes_and_mtimes(self):
        with tempfile.TemporaryDirectory() as directory:
            output = Path(directory) / "inputs"
            with mock.patch.object(fixture, "export_payloads", return_value=self.payloads):
                self.assertEqual(fixture.export(output), output.resolve() / "manifest.json")
                original = {name: ((output / name).read_bytes(), (output / name).stat().st_mtime_ns)
                            for name in self.payloads}
                self.assertEqual(fixture.export(output), output.resolve() / "manifest.json")
            for name, (payload, modified) in original.items():
                self.assertEqual(payload, self.payloads[name])
                self.assertEqual((output / name).read_bytes(), payload)
                self.assertEqual((output / name).stat().st_mtime_ns, modified)

    def test_last_destination_conflict_is_rejected_before_earlier_files_are_written(self):
        with tempfile.TemporaryDirectory() as directory:
            output = Path(directory) / "inputs"
            conflict = output / "u4l2/after.json"
            conflict.parent.mkdir(parents=True)
            conflict.write_bytes(b"preserve an existing different file")
            with mock.patch.object(fixture, "export_payloads", return_value=self.payloads):
                with self.assertRaises(FileExistsError):
                    fixture.export(output)
            self.assertEqual({path.relative_to(output).as_posix() for path in output.rglob("*")
                              if path.is_file()}, {"u4l2/after.json"})
            self.assertEqual(conflict.read_bytes(), b"preserve an existing different file")

    def test_corrupt_last_publication_cannot_leave_partial_outputs(self):
        by_ref = {fixture.PUBLISHED_COMMIT + ":" + publication["path"]:
                  self.payloads[f"{name}/before.json"]
                  for name, publication in fixture.PUBLICATIONS.items()}
        by_ref[fixture.PUBLISHED_COMMIT + ":" + fixture.PUBLICATIONS["u4l2"]["path"]] += b" "
        with tempfile.TemporaryDirectory() as directory:
            output = Path(directory) / "inputs"
            with mock.patch.object(fixture.subprocess, "check_output",
                                   side_effect=lambda command, **_kwargs: by_ref[command[-1]]):
                with self.assertRaisesRegex(ValueError, "u4l2"):
                    fixture.export(output)
            self.assertFalse(output.exists())



class ActiveInputDecisionAuthoringTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.payloads = fixture.active_payloads()
        cls.manifest = state.parse_json(cls.payloads["manifest.json"].decode("utf-8"))
        cls.lessons = {lesson["id"]: lesson for lesson in cls.manifest["lessons"]}

    def document(self, name, version):
        return state.parse_json(self.payloads[f"{name}/{version}.json"].decode("utf-8"))

    def test_six_current_publications_are_exact_git_bytes_without_fallback(self):
        self.assertEqual(fixture.ACTIVE_INPUT_COMMIT,
                         "39ba3cb196e196bac99c2d891b488f765679ad6d")
        self.assertEqual(tuple(fixture.ACTIVE_INPUT_PUBLICATIONS),
                         ("u3l3", "u3l4", "u3l5", "u3l6", "u3l7", "u3l8"))
        self.assertEqual(self.manifest["kind"], "active-input-decision")
        self.assertEqual(self.manifest["publishedCommit"], fixture.ACTIVE_INPUT_COMMIT)
        for name, publication in fixture.ACTIVE_INPUT_PUBLICATIONS.items():
            with self.subTest(lesson=name):
                original = self.payloads[f"{name}/before.json"]
                self.assertEqual(fixture.verify_active_bytes(name, original), original)
                self.assertEqual(hashlib.sha256(original).hexdigest(), publication["sha256"])
                self.assertEqual(hashlib.sha1(b"blob " + str(len(original)).encode("ascii") +
                                             b"\0" + original).hexdigest(), publication["blobSha"])
                for invalid in (original + b" ", original[:-1], self.payloads[f"{name}/after.json"]):
                    with self.assertRaises(ValueError):
                        fixture.verify_active_bytes(name, invalid)
                with mock.patch.object(fixture.subprocess, "check_output", return_value=original) as read:
                    self.assertEqual(fixture.active_published_bytes(name), original)
                command, options = read.call_args
                self.assertEqual(command[0], ["git", "--no-optional-locks", "-c",
                    f"safe.directory={fixture.ROOT}", "show",
                    f"{fixture.ACTIVE_INPUT_COMMIT}:{publication['path']}"])
                self.assertEqual(options["cwd"], fixture.ROOT)
                for variable, value in (("GIT_NO_LAZY_FETCH", "1"),
                                        ("GIT_NO_REPLACE_OBJECTS", "1"),
                                        ("GIT_TERMINAL_PROMPT", "0")):
                    self.assertEqual(options["env"][variable], value)
        with mock.patch.object(fixture.subprocess, "check_output",
                               side_effect=subprocess.CalledProcessError(128, ["git", "show"])):
            with self.assertRaisesRegex(RuntimeError, "no fetch or current-asset fallback"):
                fixture.active_published_bytes("u3l3")

    def test_current_owners_keep_45_public_cases_and_34_private_observations(self):
        owners = {"u3l3": "block_0005_0002", "u3l4": "block_0005_0002",
                  "u3l5": "block_0005_0002", "u3l6": "block_0006_0002",
                  "u3l7": "block_0007_0002", "u3l8": "block_0005_0002"}
        counts = {"u3l3": (4, 3), "u3l4": (4, 3), "u3l5": (8, 7),
                  "u3l6": (7, 6), "u3l7": (9, 8), "u3l8": (13, 7)}
        public_total = private_total = 0
        for name, identity in owners.items():
            with self.subTest(lesson=name):
                before, after = self.document(name, "before"), self.document(name, "after")
                owner = state._blocks(before)[identity]
                self.assertEqual(len(self.lessons[name]["plans"]), 1)
                plan = self.lessons[name]["plans"][0]
                declaration = after["compilerInputChecks"][-1]
                expected = deepcopy(fixture.declarations(name)[0])
                expected["activity"] = identity
                self.assertEqual(plan["activity"], identity)
                self.assertEqual(declaration, expected)
                self.assertEqual(plan["publicCases"], fixture.public_cases(name))
                self.assertEqual(owner["outputCheck"], {
                    "inputComparison": "tokens", "cases": plan["publicCases"]})
                self.assertEqual(owner["initialStandardInput"], plan["publicCases"][0]["standardInput"])
                self.assertEqual((len(plan["publicCases"]), len(declaration["probes"])), counts[name])
                self.assertEqual(declaration.get("allowSameOutput", False),
                                 name in ("u3l5", "u3l6", "u3l7"))
                self.assertEqual(plan["publicCasesSha256"],
                                 fixture.sha256(fixture.json_bytes(plan["publicCases"])))
                for case in plan["publicCases"]:
                    self.assertEqual(case["outputs"], [case["outputs"][0], case["outputs"][0] + "\n"])
                public_total += len(plan["publicCases"])
                private_total += len(declaration["probes"])
        self.assertEqual((len(self.lessons), public_total, private_total), (6, 45, 34))

    def test_append_preserves_existing_checks_and_every_other_field(self):
        for name in self.lessons:
            with self.subTest(lesson=name):
                before, after = self.document(name, "before"), self.document(name, "after")
                self.assertEqual(after["compilerInputChecks"][:-1], before.get("compilerInputChecks", []))
                preserved = deepcopy(after)
                preserved["compilerInputChecks"].pop()
                if "compilerInputChecks" not in before:
                    del preserved["compilerInputChecks"]
                self.assertEqual(preserved, before)
                self.assertEqual(state._blocks(before), state._blocks(after))
                self.assertEqual(before["courseEdition"], after["courseEdition"])
                self.assertEqual(before["stateLayout"], after["stateLayout"])
                self.assertEqual(before["compilerLocalIntChecks"], after["compilerLocalIntChecks"])
                state.validate_transition(before, after)
                with self.assertRaises(state.LayoutError):
                    state.validate_transition(after, before)
                changed = deepcopy(after)
                changed["compilerInputChecks"][-1]["probes"][0]["outputs"] = ["changed oracle"]
                with self.assertRaises(state.LayoutError):
                    state.validate_transition(after, changed)
                changed = deepcopy(after)
                changed["compilerInputChecks"][-1]["activity"] = "missing_active_owner"
                with self.assertRaises(state.LayoutError):
                    state.validate_transition(before, changed)

    def test_all_thirteen_current_compilers_have_their_audited_real_solution(self):
        total = 0
        for name, publication in fixture.ACTIVE_INPUT_PUBLICATIONS.items():
            with self.subTest(lesson=name):
                before = self.document(name, "before")
                blocks = state._blocks(before)
                owners = {key for key, value in blocks.items() if value["type"] == "cpp_compiler"}
                authored = {entry["activity"]: entry["blocks"] for entry in before["referenceAfter"]}
                solutions = self.lessons[name]["solutions"]
                self.assertEqual({entry["activity"] for entry in solutions}, owners)
                self.assertEqual(len(solutions), 3 if name == "u3l4" else 2)
                for entry in solutions:
                    identity = entry["activity"]
                    reference = publication["references"][identity]
                    self.assertIn(reference, authored[identity])
                    self.assertEqual(entry["source"], blocks[reference]["code"])
                    self.assertEqual(entry["sourceSha256"], publication["referenceSha256"][identity])
                    self.assertEqual(fixture.sha256(entry["source"].encode("utf-8")), entry["sourceSha256"])
                total += len(solutions)
        self.assertEqual(total, 13)
        self.assertEqual(fixture.ACTIVE_INPUT_PUBLICATIONS["u3l8"]["references"],
                         {"block_0002_0002": "block_0002_0006", "block_0005_0002": "block_0005_0010"})
        incomplete = deepcopy(fixture.ACTIVE_INPUT_PUBLICATIONS)
        del incomplete["u3l4"]["references"]["u3l4_alcance_editor"]
        with mock.patch.object(fixture, "ACTIVE_INPUT_PUBLICATIONS", incomplete):
            with self.assertRaisesRegex(ValueError, "omitted an actual compiler"):
                fixture.active_payloads()

    def test_u3l4_retains_the_required_scope_editor_and_authored_choice(self):
        before, after = self.document("u3l4", "before"), self.document("u3l4", "after")
        blocks = state._blocks(before)
        self.assertTrue(before["compilerPuzzlesRequired"])
        self.assertEqual([entry["activity"] for entry in before["compilerInputChecks"]],
                         ["u3l4_alcance_editor"])
        self.assertEqual(after["compilerInputChecks"][0], before["compilerInputChecks"][0])
        self.assertEqual([probe["standardInput"] for probe in before["compilerInputChecks"][0]["probes"]],
                         ["0", "8"])
        self.assertEqual(fixture.ACTIVE_INPUT_PUBLICATIONS["u3l4"]["references"]["u3l4_alcance_editor"],
                         "u3l4_alcance_solucion")
        puzzle = blocks["u3l4_alcance_codigo_en_recuadros"]
        self.assertEqual(puzzle["type"], "math_puzzle")
        self.assertEqual(len(puzzle["responses"]), 1)
        response = puzzle["responses"][0]
        self.assertEqual((response["id"], response["type"]),
                         ("posicion", "math_multiple_choice_single"))
        self.assertEqual(response["validator"], {"type": "choice", "expected": ["antes_de_return"]})
        self.assertEqual({option["id"] for option in response["options"]},
                         {"antes", "antes_de_return", "en_if"})
        self.assertEqual(state._blocks(after)["u3l4_alcance_codigo_en_recuadros"], puzzle)

    def test_current_invariants_preserve_the_five_independent_counterexamples(self):
        expected = {"u3l3": {}, "u3l4": {},
                    "u3l5": {"open_only": ["0 0 1"],
                             "invitation_ignores_closure": ["0 1 0", "1 1 0"]},
                    "u3l6": {"nonzero_instead_of_positive": ["28 -1"]},
                    "u3l7": {"no_upper_bound": ["13 1"],
                             "zero_allowed_with_permission": ["0 1"]}, "u3l8": {}}
        total = 0
        for name, labels in expected.items():
            plan = self.lessons[name]["plans"][0]
            negatives = plan["invariantCounterexamples"]
            self.assertEqual({entry["label"]: entry["failedInputs"] for entry in negatives}, labels)
            for negative in negatives:
                pin = next(entry for entry in fixture.INVARIANT_COUNTEREXAMPLES[name]
                           if entry["label"] == negative["label"])
                self.assertEqual(negative["sourceSha256"], pin["sourceSha256"])
                self.assertEqual(fixture.sha256(negative["source"].encode("utf-8")), pin["sourceSha256"])
            total += len(negatives)
        self.assertEqual(total, 5)

    def test_active_export_is_idempotent_and_the_modes_are_exclusive(self):
        with tempfile.TemporaryDirectory() as directory:
            output = Path(directory) / "active"
            with mock.patch.object(fixture, "active_payloads", return_value=self.payloads) as active:
                with self.assertRaisesRegex(ValueError, "not both"):
                    fixture.export(output, active=True, active_loops=True)
                active.assert_not_called()
                self.assertFalse(output.exists())
                self.assertEqual(fixture.export(output, active=True), output.resolve() / "manifest.json")
                saved = {name: ((output / name).read_bytes(), (output / name).stat().st_mtime_ns)
                         for name in self.payloads}
                fixture.export(output, active=True)
            for name, (payload, modified) in saved.items():
                self.assertEqual(payload, self.payloads[name])
                self.assertEqual((output / name).read_bytes(), payload)
                self.assertEqual((output / name).stat().st_mtime_ns, modified)

    def test_corrupt_last_active_source_cannot_leave_partial_outputs(self):
        by_ref = {fixture.ACTIVE_INPUT_COMMIT + ":" + publication["path"]:
                  self.payloads[f"{name}/before.json"]
                  for name, publication in fixture.ACTIVE_INPUT_PUBLICATIONS.items()}
        by_ref[fixture.ACTIVE_INPUT_COMMIT + ":" + fixture.ACTIVE_INPUT_PUBLICATIONS["u3l8"]["path"]] += b" "
        with tempfile.TemporaryDirectory() as directory:
            output = Path(directory) / "active"
            with mock.patch.object(fixture.subprocess, "check_output",
                                   side_effect=lambda command, **_kwargs: by_ref[command[-1]]):
                with self.assertRaisesRegex(ValueError, "u3l8"):
                    fixture.export(output, active=True)
            self.assertFalse(output.exists())


if __name__ == "__main__":
    unittest.main()
