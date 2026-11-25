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


class ActiveLocalDependencyViewAuthoringTests(unittest.TestCase):
    EXPECTED_FOCAL = {
        "u2l1": {"block_0004_0002": "block_0005_0001", "block_0004_0006": "block_0006_0003"},
        "u2l2": {"block_0004_0003": "block_0005_0001", "block_0004_0006": "block_0006_0003"},
        "u2l3": {"block_0006_0002": "block_0007_0001", "block_0006_0006": "block_0008_0003"},
        "u2l7": {"block_0006_0002": "block_0007_0001", "block_0008_0006": "block_0008_0008"},
        "u2l8": {"block_0006_0002": "block_0007_0001"},
        "u2l9": {"block_0006_0004": "block_0006_0006", "block_0009_0004": "block_0009_0006"},
        "u2l10": {"block_0004_0005": "block_0004_0007", "block_0007_0004": "block_0007_0006",
                   "block_0010_0002": "block_0010_0006"},
        "u3l1": {"block_0005_0002": "block_0005_0005"},
        "u3l2": {"block_0005_0002": "block_0005_0005"},
    }

    @classmethod
    def setUpClass(cls):
        # Real Git bytes and all production transition validators run before
        # the I/O-only cases reuse these deterministic, isolated documents.
        cls.payloads = fixture.active_payloads()
        cls.manifest = state.parse_json(cls.payloads["manifest.json"].decode("utf-8"))
        cls.lessons = {item["id"]: item for item in cls.manifest["lessons"]}

    def document(self, name, version):
        return state.parse_json(self.payloads[f"{name}/{version}.json"].decode("utf-8"))

    def test_active_git_reads_authenticate_current_publications_without_fallback(self):
        self.assertEqual(fixture.ACTIVE_PUBLISHED_COMMIT,
                         "39ba3cb196e196bac99c2d891b488f765679ad6d")
        for name, spec in fixture.ACTIVE_PUBLICATIONS.items():
            with self.subTest(lesson=name):
                original = self.payloads[f"{name}/before.json"]
                self.assertEqual(hashlib.sha256(original).hexdigest(), spec["sha256"])
                self.assertEqual(original, fixture.verify_active_published_bytes(name, original))
                with self.assertRaises(ValueError):
                    fixture.verify_active_published_bytes(name, original + b" ")
                with mock.patch.object(fixture.subprocess, "check_output", return_value=original) as read:
                    self.assertEqual(original, fixture.active_published_bytes(name))
                args, kwargs = read.call_args
                self.assertEqual(args[0], [
                    "git", "--no-optional-locks", "-c", f"safe.directory={fixture.ROOT}",
                    "show", f"{fixture.ACTIVE_PUBLISHED_COMMIT}:{spec['path']}"])
                self.assertEqual(kwargs["cwd"], fixture.ROOT)
                self.assertEqual(kwargs["env"]["GIT_NO_LAZY_FETCH"], "1")
                self.assertEqual(kwargs["env"]["GIT_NO_REPLACE_OBJECTS"], "1")
                self.assertEqual(kwargs["env"]["GIT_TERMINAL_PROMPT"], "0")
                # Even a self-consistent replacement digest cannot authorize
                # another edition, language or lesson in this pinned cohort.
                for field, replacement in (("id", "wrong_owner"),
                                           ("courseEdition", {**fixture.ACTIVE_EDITION, "generation": 2})):
                    changed = self.document(name, "before")
                    changed[field] = replacement
                    payload = fixture.json_bytes(changed)
                    with mock.patch.dict(spec, {"sha256": hashlib.sha256(payload).hexdigest()}):
                        with self.assertRaisesRegex(ValueError, "course edition"):
                            fixture.verify_active_published_bytes(name, payload)

    def test_nine_active_transitions_preserve_every_existing_field_and_private_root(self):
        self.assertEqual(set(self.manifest), {"version", "publishedCommit", "kind", "lessons"})
        self.assertEqual(self.manifest["kind"], "active-local-dependency")
        self.assertEqual(self.manifest["publishedCommit"], fixture.ACTIVE_PUBLISHED_COMMIT)
        self.assertEqual(list(self.lessons), list(self.EXPECTED_FOCAL))
        self.assertEqual(len(self.payloads), 19)
        plans = solutions = 0
        for name, entry in self.lessons.items():
            with self.subTest(lesson=name):
                before, after = self.document(name, "before"), self.document(name, "after")
                self.assertEqual(before["courseEdition"], fixture.ACTIVE_EDITION)
                self.assertIn("__edition_es_1__", before["id"])
                state.validate_transition(before, after)
                stripped = deepcopy(after)
                for root, declaration in fixture.active_declarations(name).items():
                    self.assertNotIn(root, before)
                    self.assertEqual(stripped.pop(root), declaration)
                self.assertEqual(stripped, before)
                with self.assertRaises(state.LayoutError):
                    state.validate_transition(after, before)
                self.assertEqual({item["activity"]: item["reference"] for item in entry["plans"]},
                                 self.EXPECTED_FOCAL[name])
                editors = {identity for identity, block in state._blocks(before).items()
                           if block.get("type") == "cpp_compiler"}
                self.assertEqual({item["activity"] for item in entry["solutions"]}, editors)
                self.assertEqual(len(entry["solutions"]), len(editors))
                for version in ("before", "after"):
                    self.assertEqual(entry[version], f"{name}/{version}.json")
                    self.assertEqual(entry[version + "Sha256"],
                                     hashlib.sha256(self.payloads[entry[version]]).hexdigest())
                plans += len(entry["plans"])
                solutions += len(entry["solutions"])
        self.assertEqual((plans, solutions), (16, 18))
        for name, repair in (("u3l1", "block_0003_0002"), ("u3l2", "block_0004_0003")):
            before, after = self.document(name, "before"), self.document(name, "after")
            self.assertEqual(before["compilerLocalIntChecks"], after["compilerLocalIntChecks"])
            self.assertIn(repair, {item["activity"] for item in after["compilerLocalIntChecks"]})
            self.assertNotIn(repair, self.EXPECTED_FOCAL[name])

    def test_active_declarations_reuse_exact_probes_and_preserve_optional_cases(self):
        for name in self.lessons:
            with self.subTest(lesson=name):
                current = fixture.active_declarations(name)
                reverse = {target: old for old, (target, _) in
                           fixture.ACTIVE_PUBLICATIONS[name]["owners"].items()}
                for entries in current.values():
                    for entry in entries:
                        entry["activity"] = reverse[entry["activity"]]
                self.assertEqual(current, fixture.declarations(name))
                before = state._blocks(self.document(name, "before"))
                after = state._blocks(self.document(name, "after"))
                for identity in self.EXPECTED_FOCAL[name]:
                    self.assertEqual(before[identity], after[identity])
                    self.assertTrue(after[identity]["outputCheck"]["cases"])
        repaired = state._blocks(self.document("u2l10", "after"))["block_0010_0002"]
        self.assertEqual([case["standardInput"] for case in repaired["outputCheck"]["cases"]],
                         ["4 5 2", "0 2 1", "6 0 2"])

    def test_active_solutions_use_their_own_references_and_four_exact_starter_repairs(self):
        repaired = published = 0
        for name, entry in self.lessons.items():
            before = self.document(name, "before")
            blocks = state._blocks(before)
            references = {item["activity"]: item["blocks"] for item in before["referenceAfter"]}
            reverse = {current: (old, reference) for old, (current, reference) in
                       fixture.ACTIVE_PUBLICATIONS[name]["owners"].items()}
            for solution in entry["solutions"]:
                activity, source = solution["activity"], solution["source"]
                with self.subTest(lesson=name, activity=activity):
                    old, reference = reverse[activity]
                    self.assertIn(reference, references[activity])
                    self.assertEqual(blocks[reference]["language"], "cpp")
                    self.assertEqual(solution["sourceSha256"], hashlib.sha256(source.encode()).hexdigest())
                    if (name, old) in fixture.ACTIVE_STARTER_REPAIRS:
                        expected = blocks[activity]["initialCode"]
                        for previous, revised in fixture.ACTIVE_STARTER_REPAIRS[name, old]:
                            self.assertEqual(expected.count(previous), 1)
                            expected = expected.replace(previous, revised, 1)
                        self.assertEqual(source, expected)
                        repaired += 1
                    else:
                        self.assertEqual(source, blocks[reference]["code"])
                        published += 1
        self.assertEqual((repaired, published), (4, 14))
        u2 = next(item["source"] for item in self.lessons["u2l3"]["solutions"]
                  if item["activity"] == "block_0006_0006")
        self.assertIn("aforo += 3;\n\n    int anterior = aforo;", u2)
        u3 = next(item["source"] for item in self.lessons["u3l1"]["solutions"]
                  if item["activity"] == "block_0003_0002")
        self.assertIn("sobres * fichas_por_sobre + fichas_sueltas", u3)
        self.assertNotRegex(u3, r"\bsueltas\b")

    def test_active_fixed_reports_keep_source_outside_output_and_one_role_without_plan(self):
        output = re.compile(r"\bstd::cout\s*<<[^;]*;")
        no_plan = []
        for name, entry in self.lessons.items():
            sources = {item["activity"]: item["source"] for item in entry["solutions"]}
            blocks = state._blocks(self.document(name, "before"))
            for plan in entry["plans"]:
                activity, fixed = plan["activity"], plan["fixedSource"]
                with self.subTest(lesson=name, activity=activity):
                    self.assertEqual(output.sub("<output>", fixed),
                                     output.sub("<output>", sources[activity]))
                    self.assertEqual(plan["fixedSourceSha256"], hashlib.sha256(fixed.encode()).hexdigest())
                    expected = blocks[activity]["outputCheck"]["cases"][0]["outputs"][0]
                    prefix = "std::cout << " + ("etiqueta.substr(0, 0) << " if name == "u2l7" else "")
                    self.assertEqual(output.findall(fixed)[0], prefix + json.dumps(expected) + ";")
                    if "noPlanSource" in plan:
                        no_plan.append((name, activity))
                        self.assertEqual(plan["noPlanSource"], fixture.no_plan_source(expected))
                        self.assertEqual(plan["noPlanSourceSha256"],
                                         hashlib.sha256(plan["noPlanSource"].encode()).hexdigest())
        self.assertEqual(no_plan, [("u2l10", "block_0004_0005")])

    def test_active_rejects_archived_owners_foreign_references_and_existing_root_replacement(self):
        for name, old, wrong in (("u2l2", "block_0004_0002", "block_0004_0002"),
                                 ("u2l7", "block_0008_0007", "block_0008_0007"),
                                 ("u2l10", "block_0004_0002", "block_0004_0002")):
            with self.subTest(lesson=name):
                owners = deepcopy(fixture.ACTIVE_PUBLICATIONS[name]["owners"])
                owners[old] = (wrong, owners[old][1])
                with mock.patch.dict(fixture.ACTIVE_PUBLICATIONS[name], {"owners": owners}):
                    with self.assertRaises(ValueError):
                        fixture.lesson_payloads(name, active=True)
        owners = deepcopy(fixture.ACTIVE_PUBLICATIONS["u2l1"]["owners"])
        owners["block_0004_0002"] = ("block_0004_0002", "block_0006_0003")
        with mock.patch.dict(fixture.ACTIVE_PUBLICATIONS["u2l1"], {"owners": owners}):
            with self.assertRaisesRegex(ValueError, "authored reference"):
                fixture.lesson_payloads("u2l1", active=True)
        added = fixture.active_declarations("u3l1")
        added["compilerLocalIntChecks"] = []
        with mock.patch.object(fixture, "active_declarations", return_value=added):
            with self.assertRaisesRegex(ValueError, "already contains"):
                fixture.lesson_payloads("u3l1", active=True)

    def test_active_export_preserves_bytes_and_detects_late_conflicts_before_writing(self):
        with tempfile.TemporaryDirectory() as directory:
            output = Path(directory) / "active"
            with mock.patch.object(fixture, "active_payloads", return_value=self.payloads):
                fixture.export(output, active=True)
                before = {name: ((output / name).read_bytes(), (output / name).stat().st_mtime_ns)
                          for name in self.payloads}
                fixture.export(output, active=True)
            for name, (payload, modified) in before.items():
                self.assertEqual(payload, self.payloads[name])
                self.assertEqual((output / name).read_bytes(), payload)
                self.assertEqual((output / name).stat().st_mtime_ns, modified)
        with tempfile.TemporaryDirectory() as directory:
            output = Path(directory) / "active"
            conflict = output / "u3l2/after.json"
            conflict.parent.mkdir(parents=True)
            conflict.write_bytes(b"preserve existing content")
            with mock.patch.object(fixture, "active_payloads", return_value=self.payloads):
                with self.assertRaises(FileExistsError):
                    fixture.export(output, active=True)
            self.assertEqual(conflict.read_bytes(), b"preserve existing content")
            self.assertEqual({p.relative_to(output).as_posix() for p in output.rglob("*") if p.is_file()},
                             {"u3l2/after.json"})

    def test_active_missing_git_or_corrupted_last_publication_creates_no_output(self):
        by_ref = {fixture.ACTIVE_PUBLISHED_COMMIT + ":" + spec["path"]:
                  self.payloads[f"{name}/before.json"]
                  for name, spec in fixture.ACTIVE_PUBLICATIONS.items()}
        last_ref = fixture.ACTIVE_PUBLISHED_COMMIT + ":" + fixture.ACTIVE_PUBLICATIONS["u3l2"]["path"]
        by_ref[last_ref] += b" "
        with tempfile.TemporaryDirectory() as directory:
            output = Path(directory) / "active"
            with mock.patch.object(fixture.subprocess, "check_output", side_effect=lambda command, **_: by_ref[command[-1]]):
                with self.assertRaisesRegex(ValueError, "u3l2"):
                    fixture.export(output, active=True)
            self.assertFalse(output.exists())
            failure = fixture.subprocess.CalledProcessError(128, ["git", "show"])
            with mock.patch.object(fixture.subprocess, "check_output", side_effect=failure):
                with self.assertRaises(fixture.subprocess.CalledProcessError):
                    fixture.export(output, active=True)
            self.assertFalse(output.exists())

    def test_default_historical_manifest_remains_byte_exact(self):
        self.assertEqual(hashlib.sha256(fixture.export_payloads()["manifest.json"]).hexdigest(),
                         "ed6a5302fc928db8302ded44a4ff61a10decd58d2b30f70344977a871a7ef789")


if __name__ == "__main__":
    unittest.main()
