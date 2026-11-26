"""Authoring/provenance guards for the real nested-CMake Windows fixtures.

These tests validate publications and format contracts. MSVC/CMake execution,
native profile storage and selective history recovery belong to the companion
native.nested_cmake_format test, not to this Python authoring check.
"""
from __future__ import annotations

from copy import deepcopy
import hashlib
import json
from pathlib import Path
import tempfile
import unittest
from unittest import mock

import code_format_compat as source_format
from format_batch_history import active_owners, assert_stable_storage
import nested_cmake_format_fixture_source as fixtures
import stable_state_layout as state
import validar_json_programacion as contract
import compiler_case_revision_fixture_source as case_fixtures
import selective_recovery_fixture_source as history_fixtures
from published_adoption_fixtures import before_adoption


class NestedCmakeFormat(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.lessons = fixtures.build_lessons()
        cls.control, cls.invalid = fixtures.counterexamples(cls.lessons)

    def test_complete_publications_and_family_are_byte_authenticated(self):
        for name, hashes in fixtures.PUBLICATIONS.items():
            for locale, expected in hashes.items():
                with self.subTest(lesson=name, locale=locale):
                    raw = fixtures.historical_bytes(name, locale)
                    self.assertEqual(hashlib.sha256(raw).hexdigest(), expected)
                    document = json.loads(raw)
                    self.assertEqual(document["id"], self.lessons[name].before["id"])
                    if locale == "es":
                        self.assertEqual(raw, self.lessons[name].original_bytes)
                    # A valid JSON suffix still changes the complete publication.
                    with mock.patch.object(fixtures, "publication_bytes", return_value=raw + b" "):
                        with self.assertRaisesRegex(ValueError, "pinned complete publication"):
                            fixtures.historical_bytes(name, locale)
        with mock.patch.object(fixtures, "publication_bytes") as fetch:
            with self.assertRaisesRegex(ValueError, "Unknown nested-CMake publication"):
                fixtures.historical_bytes("unpublished")
            with self.assertRaisesRegex(ValueError, "Unknown nested-CMake publication"):
                fixtures.historical_bytes(next(iter(fixtures.PUBLICATIONS)), "de")
            fetch.assert_not_called()

    def test_ten_real_targets_change_only_one_leading_lf(self):
        self.assertEqual(len(fixtures.CASES), 10)
        self.assertEqual(len({(case["lesson"], case["activity"]) for case in fixtures.CASES}), 10)
        self.assertEqual([sum(case["lesson"] == name for case in fixtures.CASES)
                          for name in fixtures.PUBLICATIONS], [4, 3, 3])
        for name, lesson in self.lessons.items():
            with self.subTest(lesson=name):
                before, after = lesson.before, lesson.after
                old, current = active_owners(before)[0], active_owners(after)[0]
                restored = deepcopy(after)
                restored_blocks = active_owners(restored)[0]
                targets = [case for case in fixtures.CASES if case["lesson"] == name]
                for case in targets:
                    identity, path = case["activity"], case["path"]
                    old_file = fixtures.project_file(old[identity], path)
                    current_file = fixtures.project_file(current[identity], path)
                    self.assertEqual(current_file["initialCode"], "\n" + old_file["initialCode"])
                    self.assertTrue(source_format.require_source_format(old[identity], current[identity]))
                    projected = source_format.source_projection(current[identity], old[identity])
                    self.assertEqual(projected, old[identity])
                    self.assertEqual(state.state_fingerprint(projected), state.state_fingerprint(old[identity]))
                    # Keep the empty CMake editor alias, authored file order,
                    # selected target, stdin and every output/exit oracle exact.
                    self.assertEqual(current[identity]["initialCode"], old[identity]["initialCode"])
                    self.assertEqual(current[identity]["initialCode"], "")
                    for field in ("sourceFiles", "cmake", "initialStandardInput", "outputCheck", "compileCheck"):
                        self.assertEqual(current[identity].get(field), old[identity].get(field), field)
                    self.assertEqual([file["path"] for file in current[identity]["projectFiles"]],
                                     [file["path"] for file in old[identity]["projectFiles"]])
                    index = next(i for i, file in enumerate(old[identity]["projectFiles"])
                                 if file["path"] == path)
                    self.assertEqual(source_format.code_slots(current[identity])[
                        f"projectFiles/{index}/initialCode"][0], "cmake")
                    fixtures.project_file(restored_blocks[identity], path)["initialCode"] = old_file["initialCode"]
                restored["stateLayout"]["editorial"] = deepcopy(before["stateLayout"]["editorial"])
                self.assertEqual(restored, before, "The fixture changed more than ten source slots and their new edges")

    def test_history_storage_and_existing_source_archives_remain_exact(self):
        for name, lesson in self.lessons.items():
            with self.subTest(lesson=name):
                before, after = lesson.before, lesson.after
                owners = [case["activity"] for case in fixtures.CASES if case["lesson"] == name]
                state.validate_transition(before, after)
                self.assertEqual(state.validate(before), state.validate(after))
                assert_stable_storage(self, before, after, active_owners(before)[0])
                for field in ("origin", "family", "current", "additions", "sourceFormatRevisions"):
                    self.assertEqual(before["stateLayout"][field], after["stateLayout"][field], field)
                prior = before["stateLayout"]["editorial"]
                current = after["stateLayout"]["editorial"]
                self.assertEqual(current[:len(prior)], prior)
                self.assertEqual(len(current), len(prior) + len(owners))
                old_blocks, new_blocks = active_owners(before)[0], active_owners(after)[0]
                expected = [{"id": owner, "beforeSha256": state.fingerprint(old_blocks[owner]),
                             "afterSha256": state.fingerprint(new_blocks[owner])} for owner in owners]
                self.assertCountEqual(current[len(prior):], expected)
                self.assertTrue(set(owners) <= {
                    entry["id"] for entry in before["stateLayout"]["sourceFormatRevisions"]})
                self.assertEqual(after.get("textSemantics"), before.get("textSemantics"))

    def test_execution_edits_resolve_only_authentic_published_references(self):
        for case in fixtures.CASES:
            with self.subTest(lesson=case["lesson"], owner=case["activity"]):
                lesson = self.lessons[case["lesson"]]
                original = active_owners(lesson.before)[0][case["activity"]]
                formatted = active_owners(lesson.after)[0][case["activity"]]
                before, after = deepcopy(original), deepcopy(formatted)
                self.assertEqual(len({edit["path"] for edit in case["executionEdits"]}),
                                 len(case["executionEdits"]))
                for edit in case["executionEdits"]:
                    source = fixtures.execution_source(lesson.before, edit)
                    self.assertTrue(source)
                    # The native harness applies these exact published bytes;
                    # when it repairs the child recipe, retain the format delta.
                    fixtures.project_file(before, edit["path"])["initialCode"] = source
                    fixtures.project_file(after, edit["path"])["initialCode"] = (
                        "\n" if edit["path"] == case["path"] else "") + source
                self.assertEqual(fixtures.project_file(after, case["path"])["initialCode"],
                                 "\n" + fixtures.project_file(before, case["path"])["initialCode"])
                self.assertTrue(source_format.require_source_format(before, after))
                self.assertEqual(before["outputCheck"], original["outputCheck"])
                self.assertEqual(after["outputCheck"], formatted["outputCheck"])
        named = self.lessons["nombrar_reglas_y_separar_acuerdos"].before
        capacity_case = next(case for case in fixtures.CASES
                             if case["lesson"].startswith("nombrar_") and case["activity"] == "block_0005_0006")
        header = next(edit for edit in capacity_case["executionEdits"] if edit["path"].endswith(".h"))
        self.assertIn("cajas de 6 plazas", fixtures.execution_source(named, header))
        reorganized = self.lessons["reorganizar_la_descripcion_conservando_el_comprobador"]
        self.assertEqual(reorganized.after["compilerWorkflowChecks"], [{
            "activity": "block_0010_0003", "kind": "cmakeSelectiveRecoveryV1"}])
        self.assertEqual(reorganized.before["compilerWorkflowChecks"], reorganized.after["compilerWorkflowChecks"])
        broken = active_owners(reorganized.before)[0]["block_0006_0003"]
        self.assertIn("STATIC componente/src/cajas.cpp", fixtures.project_file(broken, "componente/CMakeLists.txt")["initialCode"])
        recovery = next(case for case in fixtures.CASES
                        if case["lesson"].startswith("reorganizar_") and case["activity"] == "block_0010_0003")
        self.assertEqual(recovery["executionEdits"], [])

    def test_u3l2_two_case_revisions_preserve_other_nested_cmake_formats(self):
        relative = ("app/src/main/assets/content/es/programming/programming_practice_tools/"
                    "comprobaciones_repetibles/lesson_reorganizar_la_descripcion_conservando_el_comprobador.json")
        before = before_adoption(Path(__file__).resolve().parents[2] / relative)
        current = before
        owners = ("block_0004_0012", "block_0010_0003")
        path = "pruebas/cajas_casos.cpp"
        # Exercise the actual formatted Work predecessor, not an older source
        # archive. These QA trials are independent of Work's local candidates.
        for identity in owners:
            block = state._blocks(current)[identity]
            old = block["outputCheck"]["cases"][0]
            if identity == history_fixtures.OWNER:
                # The workflow contract permits precisely the seven
                # independent oracles, with case 1 first. Its original
                # aggregate is in the journal, not an eighth public case.
                current = history_fixtures.selective_history_revised(current)
                self.assertEqual(state._compiler_selective_recovery_case_mode(
                    state._blocks(current)[identity]), "individual")
            else:
                driver = next(f["initialCode"] for f in block["projectFiles"]
                              if f["path"] == path)
                marker = "\nint main() {"
                self.assertEqual(driver.count(marker), 1)
                # The two published editors use the same complete checker
                # main and comparison function. Keep the actual source
                # prefix of this owner, not the History owner's prefix.
                history_driver = history_fixtures.selective_history_client(current)
                main = history_driver[history_driver.index(marker):]
                cases = history_fixtures.selective_history_cases()
                for case in cases:
                    case["explanation"] = ("La salida esperada es " +
                                           case["outputs"][0].rstrip("\n") + ".")
                current = state.revise_compiler_client_cases(
                    current, identity,
                    {"path": path, "source": driver[:driver.index(marker)] + main},
                    cases, case_semantics=case_fixtures.authored_fields(current, identity, cases))
            record = current["stateLayout"]["compilerCaseRevisions"][-1]
            self.assertEqual(record["id"], identity)
            self.assertEqual(record["block"]["outputCheck"]["cases"], [old])
            cases = state._blocks(current)[identity]["outputCheck"]["cases"]
            self.assertEqual(len(cases), 7)
            self.assertEqual(cases[0]["standardInput"], "1\n")
            self.assertEqual({c["standardInput"] for c in cases},
                             {str(value) + "\n" for value, _ in history_fixtures.PAIRS})
        state.validate_transition(before, current)
        aligned = state.align_for_previous_validation(before, current)
        self.assertEqual(state._blocks(aligned), state._blocks(before))
        self.assertEqual(before["stateLayout"]["sourceFormatRevisions"],
                         current["stateLayout"]["sourceFormatRevisions"])
        self.assertEqual(before["compilerWorkflowChecks"], current["compilerWorkflowChecks"])
        # Independently reflow the third owner, using a valid source-only
        # publication. Its per-block historical protection must remain valid
        # even when the other two owners have their new case/client histories.
        identity = "block_0006_0003"
        proposal = deepcopy(current)
        old = state._blocks(before)[identity]
        project = state._blocks(proposal)[identity]["projectFiles"]
        file = next(f for f in project if f["path"] == "componente/CMakeLists.txt")
        file["initialCode"] = "\n" + file["initialCode"]
        # Both translated blobs are unchanged from the already authenticated
        # nested-CMake fixture publication. Supply the complete language
        # family, never waive the family guard for a test-only format edit.
        name = "reorganizar_la_descripcion_conservando_el_comprobador"
        family = {"es": current}
        for locale in ("en", "fr"):
            family[locale] = json.loads(fixtures.historical_bytes(name, locale))
        state.check_localized_family(list(family.values()), list(family))
        revised = state.prepare_v2(proposal, "es", family, current)
        state.validate_transition(before, revised)
        view = state.align_for_previous_validation(before, revised)
        new = state._blocks(view)[identity]
        self.assertTrue(contract.preserves_source_format_revision(
            relative, 6, 3, old, new, previous_document=before,
            current_document=revised, course_id="programming_practice_tools"))
        forged = deepcopy(new)
        forged["projectFiles"][1]["initialCode"] += "# not the published projection\n"
        self.assertFalse(contract.preserves_source_format_revision(
            relative, 6, 3, old, forged, previous_document=before,
            current_document=revised, course_id="programming_practice_tools"))
        for changed in owners:
            self.assertEqual(state._blocks(view)[changed], state._blocks(before)[changed])

    def test_negative_final_payloads_are_authenticated_before_rejection(self):
        expected = {
            "logic": "Tokens, literals, comments or directives changed",
            "path": "A response/code contract changed or belongs to another identity",
            "literal": "Tokens, literals, comments or directives changed",
            "filename": "Unknown project data may not be changed",
        }
        base = self.lessons["datos_y_listas_de_objetivos"].after
        for name, invalid in self.invalid.items():
            with self.subTest(negative=name):
                identity = fixtures.FAKE_OWNER if name == "filename" else fixtures.CASES[0]["activity"]
                previous = self.control if name == "filename" else base
                old, new = active_owners(previous)[0][identity], active_owners(invalid)[0][identity]
                final = next(edge for edge in reversed(invalid["stateLayout"]["editorial"])
                             if edge["id"] == identity)
                self.assertEqual(final["afterSha256"], state.fingerprint(new))
                self.assertNotEqual(state.fingerprint(old), state.fingerprint(new))
                self.assertEqual(invalid["stateLayout"]["origin"], previous["stateLayout"]["origin"])
                self.assertEqual(invalid["stateLayout"]["family"], previous["stateLayout"]["family"])
                if name == "filename":
                    addition = next(item for item in invalid["stateLayout"]["additions"]
                                    if item["id"] == identity)
                    archive = next(item["block"] for item in invalid["stateLayout"]["sourceFormatRevisions"]
                                   if item["id"] == identity)
                    self.assertEqual(addition["sha256"], state.state_fingerprint(archive))
                    self.assertEqual(final["beforeSha256"], state.fingerprint(old))
                    self.assertEqual(invalid["stateLayout"]["editorial"][:-1], previous["stateLayout"]["editorial"])
                else:
                    self.assertEqual(invalid["stateLayout"]["sourceFormatRevisions"],
                                     previous["stateLayout"]["sourceFormatRevisions"])
                    old_edges = deepcopy(previous["stateLayout"]["editorial"])
                    edge = next(edge for edge in reversed(old_edges) if edge["id"] == identity)
                    edge["afterSha256"] = state.fingerprint(new)
                    self.assertEqual(invalid["stateLayout"]["editorial"], old_edges)
                with self.assertRaisesRegex(state.LayoutError, expected[name]):
                    state.validate(invalid)
                with self.assertRaisesRegex(state.LayoutError, expected[name]):
                    state.validate_transition(previous, invalid)
                with self.assertRaises(source_format.SourceFormatError):
                    source_format.require_source_format(old, new)

    def test_fake_filename_control_is_a_separate_valid_qa_addition(self):
        base = self.lessons["datos_y_listas_de_objetivos"].after
        state.validate_transition(base, self.control)
        before, control = active_owners(base)[0], active_owners(self.control)[0]
        self.assertEqual(set(control) - set(before), {fixtures.FAKE_OWNER})
        for identity, owner in before.items():
            self.assertEqual(control[identity], owner)
        for field in ("origin", "family", "editorial", "sourceFormatRevisions"):
            self.assertEqual(self.control["stateLayout"][field], base["stateLayout"][field])
        assert_stable_storage(self, base, self.control, before)
        fake = control[fixtures.FAKE_OWNER]
        self.assertEqual(fixtures.project_file(fake, fixtures.FAKE_PATH)["initialCode"],
                         fixtures.project_file(fake, "componente/CMakeLists.txt")["initialCode"])
        self.assertIsNone(source_format.code_slots(fake)["projectFiles/6/initialCode"][0])
        self.assertNotIn(fixtures.FAKE_OWNER, {case["activity"] for case in fixtures.CASES})

    def test_cmake_basename_and_existing_extensions_keep_their_boundaries(self):
        source = 'message(STATUS "componente")\nadd_library(envios STATIC src/cajas.cpp)\n'
        for path in ("CMakeLists.txt", "componente/CMakeLists.txt", "codigo/lotes/CMakeLists.txt",
                     "cmake/targets.cmake"):
            with self.subTest(path=path):
                before = {"type": "cpp_compiler", "initialCode": "", "projectFiles": [
                    {"path": path, "initialCode": source}]}
                after = deepcopy(before)
                after["projectFiles"][0]["initialCode"] = "\n" + source
                self.assertTrue(source_format.require_source_format(before, after))
        for path in ("componente/CMakeLists.txt.bak", "componente/OtherCMakeLists.txt",
                     "CMakeLists.txt/notes.txt", "componente/cmakelists.txt"):
            with self.subTest(path=path):
                before = {"type": "cpp_compiler", "initialCode": "", "projectFiles": [
                    {"path": path, "initialCode": source}]}
                after = deepcopy(before)
                after["projectFiles"][0]["initialCode"] = "\n" + source
                self.assertIsNone(source_format.code_slots(before)["projectFiles/0/initialCode"][0])
                with self.assertRaisesRegex(source_format.SourceFormatError, "Unknown project data"):
                    source_format.require_source_format(before, after)

    def test_archived_source_cannot_be_replaced_or_discarded(self):
        lesson = self.lessons["datos_y_listas_de_objetivos"]
        owner = fixtures.CASES[0]["activity"]
        forged = deepcopy(lesson.after)
        archive = next(item for item in forged["stateLayout"]["sourceFormatRevisions"] if item["id"] == owner)
        fixtures.project_file(archive["block"], "componente/CMakeLists.txt")["initialCode"] += "# forged\n"
        with self.assertRaisesRegex(state.LayoutError, "historical answer contract"):
            state.validate(forged)
        dropped = deepcopy(lesson.after)
        dropped["stateLayout"]["sourceFormatRevisions"] = [
            item for item in dropped["stateLayout"]["sourceFormatRevisions"] if item["id"] != owner]
        with self.assertRaises(state.LayoutError):
            state.validate_transition(lesson.before, dropped)

    def test_export_manifest_exact_bytes_and_existing_output_protection(self):
        expected = {"manifest.json", "control.filename.json"}
        expected.update(f"invalid.{name}.json" for name in fixtures.INVALID_MESSAGES)
        expected.update(f"{name}.{stage}.json" for name in fixtures.PUBLICATIONS for stage in ("before", "after"))
        with tempfile.TemporaryDirectory(prefix="lgaed-nested-cmake-authoring-") as directory:
            output = Path(directory)
            manifest_path = fixtures.write_fixtures(output)
            self.assertEqual({entry.name for entry in output.iterdir()}, expected)
            manifest = json.loads(manifest_path.read_bytes())
            self.assertEqual(manifest["version"], 1)
            self.assertEqual(manifest["cases"], list(fixtures.CASES))
            self.assertEqual(len(manifest["lessons"]), 3)
            self.assertEqual(len(manifest["controls"]), 1)
            self.assertEqual({item["name"]: item["message"] for item in manifest["invalid"]},
                             fixtures.INVALID_MESSAGES)
            for item in manifest["lessons"]:
                lesson = self.lessons[item["name"]]
                self.assertEqual((output / item["before"]).read_bytes(), lesson.original_bytes)
                self.assertEqual(json.loads((output / item["after"]).read_bytes()), lesson.after)
                self.assertEqual(item["publicationCommit"], fixtures.BASELINE_COMMIT)
                self.assertEqual(item["publicationPath"], fixtures.publication_path(item["name"]))
                self.assertEqual(item["publicationSha256"], fixtures.PUBLICATIONS[item["name"]]["es"])
            snapshot = {name: ((output / name).read_bytes(), (output / name).stat().st_mtime_ns)
                        for name in expected}
            self.assertEqual(fixtures.write_fixtures(output), manifest_path)
            self.assertEqual(snapshot, {name: ((output / name).read_bytes(), (output / name).stat().st_mtime_ns)
                                       for name in expected})
            victim = output / manifest["lessons"][0]["before"]
            changed = victim.read_bytes() + b" "
            victim.write_bytes(changed)
            with self.assertRaisesRegex(FileExistsError, "Refusing to overwrite"):
                fixtures.write_fixtures(output)
            self.assertEqual(victim.read_bytes(), changed)
        with self.assertRaisesRegex(ValueError, "authored/source trees"):
            fixtures.write_fixtures(fixtures.ROOT / "app" / "src" / "nested-cmake-qa")


if __name__ == "__main__":
    unittest.main()
