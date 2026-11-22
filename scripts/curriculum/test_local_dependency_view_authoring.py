"""Authenticated inputs for the real View dependency and receipt regression."""
from copy import deepcopy
import hashlib
import json
from pathlib import Path
import re
import tempfile
import unittest
from unittest import mock

import input_dependency_fixture_source as input_fixture
import local_dependency_view_fixture_source as fixture
import local_role_fixture_source as role_fixture
import local_scalar_fixture_source as scalar_fixture
import stable_state_layout as state


class LocalDependencyViewAuthoringTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        # This performs every real transition validation before the I/O-only
        # tests reuse its deterministic payloads. No validator is mocked.
        cls.payloads = fixture.export_payloads()
        cls.manifest = state.parse_json(cls.payloads["manifest.json"].decode("utf-8"))
        cls.lessons = {item["id"]: item for item in cls.manifest["lessons"]}

    def document(self, name, version):
        return state.parse_json(self.payloads[f"{name}/{version}.json"].decode("utf-8"))

    def test_pinned_git_reads_authenticate_each_original_byte_sequence(self):
        self.assertEqual(fixture.PUBLISHED_COMMIT,
                         "2ca32b2337178d1effa3a2c81759e2493cd746d0")
        for name, publication in fixture.PUBLICATIONS.items():
            with self.subTest(lesson=name):
                original = self.payloads[f"{name}/before.json"]
                self.assertEqual(hashlib.sha256(original).hexdigest(), publication["sha256"])
                self.assertEqual(original, fixture.verify_published_bytes(name, original))
                # A semantically harmless whitespace edit is still not the
                # authenticated publication, and cannot substitute for it.
                with self.assertRaises(ValueError):
                    fixture.verify_published_bytes(name, original + b" ")
                with mock.patch.object(fixture.subprocess, "check_output", return_value=original) as read:
                    self.assertEqual(original, fixture.published_bytes(name))
                args, kwargs = read.call_args
                self.assertEqual(args[0], [
                    "git", "--no-optional-locks", "-c", f"safe.directory={fixture.ROOT}",
                    "show", f"{fixture.PUBLISHED_COMMIT}:{publication['path']}"])
                self.assertEqual(kwargs["cwd"], fixture.ROOT)
                for key in ("GIT_NO_LAZY_FETCH", "GIT_NO_REPLACE_OBJECTS"):
                    self.assertEqual(kwargs["env"][key], "1")
                self.assertEqual(kwargs["env"]["GIT_TERMINAL_PROMPT"], "0")

    def test_all_nine_real_transitions_preserve_pages_cases_state_and_history(self):
        for name in fixture.PUBLICATIONS:
            with self.subTest(lesson=name):
                before, after = self.document(name, "before"), self.document(name, "after")
                state.validate_transition(before, after)
                additions = fixture.declarations(name)
                self.assertTrue(before["compilerPuzzlesRequired"])
                self.assertEqual(before["stateLayout"]["version"], 2)
                self.assertEqual(set(after) - set(before), set(additions))
                stripped = deepcopy(after)
                for root, value in additions.items():
                    self.assertNotIn(root, before)
                    self.assertEqual(after[root], value)
                    del stripped[root]
                # This includes every case payload, initial editor/input,
                # required flag, archive, reference, reading span and address.
                self.assertEqual(stripped, before)
                for identity, block in state._blocks(before).items():
                    if block.get("type") == "cpp_compiler":
                        self.assertEqual(block, state._blocks(after)[identity])
                with self.assertRaises(state.LayoutError):
                    state.validate_transition(after, before)

    def test_manifest_has_sixteen_focal_owners_and_all_eighteen_solutions(self):
        self.assertEqual(set(self.manifest), {"version", "publishedCommit", "lessons"})
        self.assertEqual(self.manifest["version"], 1)
        self.assertEqual(self.manifest["publishedCommit"], fixture.PUBLISHED_COMMIT)
        self.assertEqual(list(self.lessons), list(fixture.PUBLICATIONS))
        self.assertEqual(len(self.payloads), 19)
        plans = solutions = 0
        for name, entry in self.lessons.items():
            with self.subTest(lesson=name):
                self.assertEqual(set(entry), {"id", "before", "after", "beforeSha256",
                                             "afterSha256", "solutions", "plans"})
                for version in ("before", "after"):
                    self.assertEqual(entry[version], f"{name}/{version}.json")
                    self.assertEqual(entry[version + "Sha256"],
                                     hashlib.sha256(self.payloads[entry[version]]).hexdigest())
                blocks = state._blocks(self.document(name, "before"))
                editors = {identity for identity, block in blocks.items()
                           if block.get("type") == "cpp_compiler"}
                self.assertEqual({item["activity"] for item in entry["solutions"]}, editors)
                self.assertEqual(len(entry["solutions"]), len(editors))
                focal = {check["activity"] for checks in fixture.declarations(name).values()
                         for check in checks}
                self.assertEqual({item["activity"] for item in entry["plans"]}, focal)
                self.assertEqual(len(entry["plans"]), len(focal))
                solutions += len(entry["solutions"])
                plans += len(entry["plans"])
        self.assertEqual((plans, solutions), (16, 18))

    def test_solutions_use_four_explicit_starter_repairs_and_fourteen_authored_references(self):
        repaired = published = 0
        for name, lesson in self.lessons.items():
            before = self.document(name, "before")
            blocks = state._blocks(before)
            references = {item["activity"]: item["blocks"] for item in before["referenceAfter"]}
            for solution in lesson["solutions"]:
                activity, source = solution["activity"], solution["source"]
                with self.subTest(lesson=name, activity=activity):
                    self.assertEqual(set(solution), {"activity", "source", "sourceSha256"})
                    self.assertEqual(hashlib.sha256(source.encode("utf-8")).hexdigest(),
                                     solution["sourceSha256"])
                    reference = fixture.PUBLICATIONS[name]["references"][activity]
                    self.assertIn(reference, references[activity])
                    self.assertEqual(blocks[reference]["type"], "code")
                    self.assertEqual(blocks[reference]["language"], "cpp")
                    if (name, activity) in fixture.STARTER_REPAIRS:
                        expected = blocks[activity]["initialCode"]
                        for old, new in fixture.STARTER_REPAIRS[name, activity]:
                            self.assertEqual(expected.count(old), 1)
                            expected = expected.replace(old, new, 1)
                        self.assertEqual(source, expected)
                        repaired += 1
                    else:
                        self.assertEqual(source, blocks[reference]["code"])
                        published += 1
        self.assertEqual((repaired, published), (4, 14))
        u3 = next(item["source"] for item in self.lessons["u3l1"]["solutions"]
                  if item["activity"] == "p1u3l1_reparar_fichas")
        self.assertIn("sobres * fichas_por_sobre + fichas_sueltas", u3)
        u2 = next(item["source"] for item in self.lessons["u2l3"]["solutions"]
                  if item["activity"] == "block_0006_0006")
        self.assertIn("aforo += 3;\n    int anterior = aforo;", u2)

    def test_existing_declarations_include_typed_scalars_and_increase_witnesses(self):
        integer = [check for name in ("u2l1", "u2l2", "u2l3")
                   for check in self.document(name, "after")["compilerLocalIntChecks"]]
        self.assertEqual(len(integer), 6)
        probes = [probe for check in integer for probe in check["probes"]]
        self.assertEqual(len(probes), 12)
        increases = [probe for probe in probes if probe.get("afterUpdate")]
        self.assertEqual(len(increases), 2)
        self.assertTrue(all(probe["expectedIncrease"] == 3 for probe in increases))
        self.assertEqual(self.document("u2l7", "after")["compilerLocalStringChecks"],
                         fixture.string_declarations())
        self.assertEqual(self.document("u2l8", "after")["compilerLocalIntChecks"],
                         scalar_fixture.declarations())
        self.assertEqual(self.document("u2l9", "after")["compilerInputChecks"],
                         input_fixture.declarations())
        for name in ("u2l10", "u3l1", "u3l2"):
            self.assertEqual(self.document(name, "after")["compilerInputChecks"],
                             input_fixture.extended_declarations(name))
        combined = self.document("u2l10", "after")
        self.assertEqual(combined["compilerLocalRoleChecks"], role_fixture.declarations())
        self.assertEqual(len(combined["compilerLocalRoleChecks"]), 2)
        self.assertEqual(len(combined["compilerInputChecks"]), 1)
        for name in ("u3l1", "u3l2"):
            self.assertEqual(self.document(name, "before")["compilerLocalIntChecks"],
                             self.document(name, "after")["compilerLocalIntChecks"])

    def test_fixed_programs_preserve_every_byte_outside_output_chains(self):
        # The pinned real references have no semicolon inside their output
        # literals. This independent check compares every surrounding byte;
        # the lexical counterexample below also exercises opaque literals.
        output = re.compile(r"\bstd::cout\s*<<[^;]*;")
        no_plan = []
        for name, lesson in self.lessons.items():
            sources = {item["activity"]: item["source"] for item in lesson["solutions"]}
            before = state._blocks(self.document(name, "before"))
            for plan in lesson["plans"]:
                activity, fixed = plan["activity"], plan["fixedSource"]
                with self.subTest(lesson=name, activity=activity):
                    source = sources[activity]
                    self.assertEqual(output.sub("<output>", source), output.sub("<output>", fixed))
                    self.assertEqual(len(output.findall(source)), len(output.findall(fixed)))
                    self.assertEqual(plan["fixedSourceSha256"], hashlib.sha256(fixed.encode()).hexdigest())
                    expected = before[activity]["outputCheck"]["cases"][0]["outputs"][0]
                    first = "std::cout << "
                    if name == "u2l7":
                        first += "etiqueta.substr(0, 0) << "
                    self.assertEqual(output.findall(fixed)[0], first + json.dumps(expected) + ";")
                    self.assertTrue(all(text == 'std::cout << "";'
                                        for text in output.findall(fixed)[1:]))
                    if "noPlanSource" in plan:
                        no_plan.append((name, activity))
                        self.assertEqual(plan["noPlanSourceSha256"],
                                         hashlib.sha256(plan["noPlanSource"].encode()).hexdigest())
                        self.assertNotRegex(plan["noPlanSource"], r"\b(?:double|float|auto)\b")
                        self.assertEqual(plan["noPlanSource"], fixture.no_plan_source(expected))
        self.assertEqual(no_plan, [("u2l10", "block_0004_0002")])

    def test_output_replacement_keeps_interleaved_updates_and_ignores_comment_literals(self):
        source = ('#include <iostream>\nint main() {\n'
                  '    int first = 1;\n    int second = 2;\n'
                  '    const char* note = "std::cout << fake;";\n'
                  '    // std::cout << not_a_statement;\n'
                  '    std::cout << "A; " << first;\n'
                  '    second += first;\n    std::cin >> first;\n'
                  '    std::cout << second;\n    return 0;\n}\n')
        expected = source.replace('std::cout << "A; " << first;',
                                  'std::cout << "A: 1\\nB: 3";').replace(
                                      "std::cout << second;", 'std::cout << "";')
        self.assertEqual(fixture.fixed_output_source(source, "A: 1\nB: 3"), expected)

    def test_output_replacement_rejects_side_effects_and_non_statement_outputs(self):
        for statement in ("std::cout << ++value;", "std::cout << calculate();",
                          "if (value) std::cout << value;", "return 0;"):
            with self.subTest(statement=statement), self.assertRaises(ValueError):
                fixture.fixed_output_source("int main(){int value=1;" + statement + "}", "1")

    def test_export_is_byte_exact_and_idempotent_for_all_nineteen_files(self):
        with tempfile.TemporaryDirectory() as directory:
            output = Path(directory) / "view"
            with mock.patch.object(fixture, "export_payloads", return_value=self.payloads):
                self.assertEqual(fixture.export(output), output.resolve() / "manifest.json")
                before = {name: (path.read_bytes(), path.stat().st_mtime_ns)
                          for name in self.payloads if (path := output / name).is_file()}
                self.assertEqual(len(before), 19)
                self.assertEqual(fixture.export(output), output.resolve() / "manifest.json")
            for name, (contents, modified) in before.items():
                self.assertEqual(contents, self.payloads[name])
                self.assertEqual((output / name).read_bytes(), contents)
                self.assertEqual((output / name).stat().st_mtime_ns, modified)

    def test_late_conflict_is_detected_before_any_other_output_is_created(self):
        with tempfile.TemporaryDirectory() as directory:
            output = Path(directory) / "view"
            conflict = output / "u3l2/after.json"
            conflict.parent.mkdir(parents=True)
            conflict.write_bytes(b"already present; preserve it")
            with mock.patch.object(fixture, "export_payloads", return_value=self.payloads):
                with self.assertRaises(FileExistsError):
                    fixture.export(output)
            self.assertEqual(conflict.read_bytes(), b"already present; preserve it")
            self.assertEqual({path.relative_to(output).as_posix()
                              for path in output.rglob("*") if path.is_file()},
                             {"u3l2/after.json"})
            self.assertFalse((output / "u2l1").exists())

    def test_last_publication_corruption_creates_no_output(self):
        by_ref = {fixture.PUBLISHED_COMMIT + ":" + publication["path"]:
                  self.payloads[f"{name}/before.json"]
                  for name, publication in fixture.PUBLICATIONS.items()}
        last_ref = fixture.PUBLISHED_COMMIT + ":" + fixture.PUBLICATIONS["u3l2"]["path"]
        by_ref[last_ref] += b" "

        def read(command, **_kwargs):
            return by_ref[command[-1]]

        with tempfile.TemporaryDirectory() as directory:
            output = Path(directory) / "view"
            with mock.patch.object(fixture.subprocess, "check_output", side_effect=read):
                with self.assertRaisesRegex(ValueError, "u3l2"):
                    fixture.export(output)
            self.assertFalse(output.exists())


if __name__ == "__main__":
    unittest.main()
