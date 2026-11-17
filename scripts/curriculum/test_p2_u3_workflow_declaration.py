"""The published U3 History owner retains its original state when declared."""
from copy import deepcopy
import importlib.util
import json
from pathlib import Path
import unittest

import selective_recovery_fixture_source as history_fixtures

root = Path(__file__).resolve().parents[2]
spec = importlib.util.spec_from_file_location(
    "stable_state_layout", Path(__file__).with_name("stable_state_layout.py"))
layout = importlib.util.module_from_spec(spec)
spec.loader.exec_module(layout)
asset = (root / "app/src/main/assets/content/es/programming/programming_practice_tools"
         "/comprobaciones_repetibles/lesson_reorganizar_la_descripcion_conservando_el_comprobador.json")
declaration = {"activity": "block_0010_0003", "kind": "cmakeSelectiveRecoveryV1"}


class CompilerWorkflowDeclarationTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.published = json.loads(asset.read_text(encoding="utf-8"))

    def original_without_declaration(self):
        result = deepcopy(self.published)
        result.pop("compilerWorkflowChecks", None)
        return result

    def revised(self):
        return deepcopy(self.published)

    def test_published_declaration_preserves_original_state_and_is_append_only(self):
        before, after = self.original_without_declaration(), self.revised()
        self.assertEqual(after.get("compilerWorkflowChecks"), [declaration])
        self.assertEqual(layout.validate(before), layout.validate(after))
        layout.validate_transition(before, after)
        self.assertEqual(before["pages"], after["pages"])
        self.assertEqual(before["stateLayout"], after["stateLayout"])
        self.assertEqual(before["compilerPuzzlesRequired"], after["compilerPuzzlesRequired"])

    def test_declaration_cannot_be_removed_or_replaced(self):
        after = self.revised()
        without = self.original_without_declaration()
        with self.assertRaises(layout.LayoutError):
            layout.validate_transition(after, without)
        renamed = deepcopy(after)
        renamed["compilerWorkflowChecks"][0]["kind"] = "anotherWorkflow"
        with self.assertRaises(layout.LayoutError):
            layout.validate_transition(after, renamed)

    def test_wrong_owner_kind_duplicates_and_types_are_rejected(self):
        for candidate in (
            [{"activity": "block_0003_0004", "kind": declaration["kind"]}],
            [{"activity": declaration["activity"], "kind": "stdoutOnly"}],
            [declaration, declaration],
            [{"activity": declaration["activity"]}],
            "block_0010_0003",
        ):
            with self.subTest(checks=candidate):
                data = self.revised()
                data["compilerWorkflowChecks"] = candidate
                with self.assertRaises(layout.LayoutError):
                    layout.validate(data)

    def test_wrong_lesson_and_target_rejected(self):
        data = self.revised()
        data["id"] = "other_lesson"
        with self.assertRaises(layout.LayoutError):
            layout.validate(data)
        data = self.revised()
        data["pages"][10]["blocks"][3]["cmake"]["target"] = "consultar_cajas"
        with self.assertRaises(layout.LayoutError):
            layout.validate(data)


class SelectiveRecoveryIndividualCasesTest(unittest.TestCase):
    """The new checker epoch preserves History and one required comparison."""

    @classmethod
    def setUpClass(cls):
        cls.layout = history_fixtures.layout
        cls.before = history_fixtures.selective_history_publication()
        cls.after = history_fixtures.selective_history_revised(cls.before)
        cls.owner = history_fixtures.OWNER
        cls.client_path = history_fixtures.PATH

    def block(self, document):
        return self.layout._blocks(document)[self.owner]

    def cases(self, document):
        return deepcopy(self.block(document)["outputCheck"]["cases"])

    def semantics(self, document):
        return self.layout._compiler_case_order_fields(document, self.owner)

    def reject_contract(self, cases=..., **config):
        # Exercise the workflow guard itself: changing a case without its
        # journal would otherwise fail earlier and hide an overbroad guard.
        document = deepcopy(self.after)
        check = self.block(document)["outputCheck"]
        if cases is not ...:
            check["cases"] = cases
        check.update(config)
        with self.assertRaises(self.layout.LayoutError):
            self.layout._validate_compiler_workflow_checks(document)

    def test_individual_revision_keeps_workflow_archives_and_all_other_files(self):
        before, after = self.before, self.after
        self.assertEqual(after["compilerWorkflowChecks"], [declaration])
        self.assertEqual(after["compilerWorkflowChecks"], before["compilerWorkflowChecks"])
        self.assertEqual(after["stateLayout"]["current"], before["stateLayout"]["current"])
        self.assertEqual(after["stateLayout"]["editorial"][:-1], before["stateLayout"]["editorial"])
        record = after["stateLayout"]["compilerCaseRevisions"][-1]
        self.assertEqual(record["block"], self.block(before))
        self.assertEqual(record["client"]["path"], self.client_path)
        self.assertEqual(record["cases"], self.cases(after))
        self.assertEqual(record["semanticBefore"], self.semantics(before))
        self.assertEqual(record["semanticAfter"], self.semantics(after))
        for old, new in zip(self.block(before)["projectFiles"], self.block(after)["projectFiles"]):
            with self.subTest(path=old["path"]):
                if old["path"] == self.client_path:
                    self.assertNotEqual(old["initialCode"], new["initialCode"])
                else:
                    self.assertEqual(old, new)
        old_blocks, new_blocks = self.layout._blocks(before), self.layout._blocks(after)
        self.assertEqual(set(old_blocks), set(new_blocks))
        for identity in old_blocks:
            if identity != self.owner:
                self.assertEqual(old_blocks[identity], new_blocks[identity])
        self.assertEqual(self.layout._compiler_selective_recovery_case_mode(self.block(before)), "aggregate")
        self.assertEqual(self.layout._compiler_selective_recovery_case_mode(self.block(after)), "individual")
        self.layout.validate_transition(before, after)

    def test_six_optional_cases_can_be_permuted_without_a_new_client(self):
        after = self.layout.reorder_compiler_cases(self.after, self.owner, [0, 6, 5, 4, 3, 2, 1])
        self.assertEqual(self.cases(after)[0], self.cases(self.after)[0])
        self.assertEqual(after["stateLayout"]["compilerCaseRevisions"],
                         self.after["stateLayout"]["compilerCaseRevisions"])
        self.assertEqual(self.block(after)["projectFiles"], self.block(self.after)["projectFiles"])
        self.layout.validate_transition(self.before, after)
        for order in ([1, 0, 2, 3, 4, 5, 6], [5, 1, 2, 3, 4, 0, 6]):
            with self.subTest(order=order), self.assertRaises(self.layout.LayoutError):
                self.layout.reorder_compiler_cases(self.after, self.owner, order)

    def test_zero_exit_and_empty_stderr_defaults_match_explicit_fields(self):
        for published in (self.before, self.after):
            for explicit in (False, True):
                with self.subTest(individual=published is self.after, explicit=explicit):
                    document = deepcopy(published)
                    for case in self.block(document)["outputCheck"]["cases"]:
                        case.pop("exitCode", None)
                        case.pop("target", None)
                        case.pop("standardErrorExact", None)
                        if explicit:
                            case.update(exitCode=0, target="comprobar_cajas", standardErrorExact="")
                    self.layout._validate_compiler_workflow_checks(document)

    def test_individual_case_set_and_canonical_input_are_closed(self):
        original = self.cases(self.after)
        for cases in (original[:6], original + [deepcopy(original[-1])],
                      original[:5] + [deepcopy(original[1]), deepcopy(original[2])],
                      [original[1], original[0]] + original[2:]):
            with self.subTest(cases=cases):
                self.reject_contract(cases)
        for stdin in ("1", "1\r\n", "+1\n", "01\n", " 1\n", "1 1\n", "2\n", "", None, 1):
            with self.subTest(stdin=stdin):
                cases = self.cases(self.after)
                cases[0]["standardInput"] = stdin
                self.reject_contract(cases)

    def test_diagnostic_summary_final_newline_and_single_output_are_required(self):
        expected = self.cases(self.after)[0]["outputs"][0]
        for outputs in (
            ["Bateria: OK\n"],
            [expected.rstrip("\n")],
            [expected.replace("obtenido=1", "obtenido=0")],
            [expected.replace(" esperado=1", " esperado=0")],
            [expected.replace(" OK\n", " FALLO\n")],
            [expected.replace("\n", "\r\n")],
            [expected, expected + "\n"],
            self.cases(self.before)[0]["outputs"],
            [], None, expected,
        ):
            with self.subTest(outputs=outputs):
                cases = self.cases(self.after)
                cases[0]["outputs"] = outputs
                self.reject_contract(cases)

    def test_other_target_exit_and_error_channels_are_rejected(self):
        for field, value in (
            ("target", "consultar_cajas"), ("target", None),
            ("exitCode", 1), ("exitCode", False), ("exitCode", "0"), ("exitCode", None),
            ("standardError", "nonBlank"), ("standardErrorExact", "\n"),
            ("standardErrorExact", None), ("required", False),
        ):
            with self.subTest(field=field, value=value):
                cases = self.cases(self.after)
                cases[0][field] = value
                self.reject_contract(cases)

    def test_malformed_output_contracts_and_semantic_overlays_are_rejected(self):
        for config in (
            {"cases": None}, {"cases": {}}, {"cases": "1"},
            {"cases": []}, {"cases": [None] * 7},
            {"inputComparison": "tokens"},
            {"inputComparison": None},
            {"someSemanticGoal": True},
        ):
            with self.subTest(config=config):
                self.reject_contract(**config)

    def test_legacy_oracle_remains_exact(self):
        for edit in (
            lambda case: case.update(standardInput="1\n"),
            lambda case: case.update(outputs=["Bateria: OK\n"]),
            lambda case: case.update(outputs=[case["outputs"][0].rstrip("\n")]),
            lambda case: case.update(outputs=[case["outputs"][0].replace("piezas=0", "piezas=1", 1)]),
            lambda case: case.update(exitCode=1),
        ):
            document = deepcopy(self.before)
            edit(self.block(document)["outputCheck"]["cases"][0])
            with self.assertRaises(self.layout.LayoutError):
                self.layout._validate_compiler_workflow_checks(document)

    def test_cases_only_and_unrecorded_source_cannot_create_the_individual_epoch(self):
        with self.assertRaises(self.layout.LayoutError):
            self.layout.revise_compiler_cases(self.before, self.owner, self.cases(self.after),
                                              case_semantics=self.semantics(self.after))
        unrecorded = deepcopy(self.before)
        self.block(unrecorded).clear()
        self.block(unrecorded).update(deepcopy(self.block(self.after)))
        with self.assertRaises(self.layout.LayoutError):
            self.layout.validate_transition(self.before, unrecorded)

    def test_authentic_return_to_aggregate_requires_a_new_individual_client_epoch(self):
        before_block = self.block(self.before)
        before_edges = [edge for edge in self.after["stateLayout"]["editorial"] if edge["id"] == self.owner]
        nodes = [before_edges[0]["beforeSha256"]] + [edge["afterSha256"] for edge in before_edges]
        returned = self.layout.revise_compiler_client_cases(
            self.after, self.owner,
            {"path": self.client_path,
             "source": self.layout._compiler_client_source(before_block, self.client_path)},
            self.cases(self.before), case_semantics=self.semantics(self.before),
            return_to=nodes.index(self.layout.fingerprint(before_block)))
        self.assertEqual(self.block(returned), before_block)
        self.assertEqual(returned["compilerWorkflowChecks"], self.before["compilerWorkflowChecks"])
        self.layout.validate_transition(self.before, returned)
        with self.assertRaises(self.layout.LayoutError):
            self.layout.revise_compiler_cases(returned, self.owner, self.cases(self.after),
                                              case_semantics=self.semantics(self.after))
        restored = self.layout.revise_compiler_client_cases(
            returned, self.owner,
            {"path": self.client_path,
             "source": self.layout._compiler_client_source(self.block(self.after), self.client_path)},
            self.cases(self.after), case_semantics=self.semantics(self.after),
            return_to=nodes.index(self.layout.fingerprint(self.block(self.after))))
        self.assertEqual(self.block(restored), self.block(self.after))
        records = restored["stateLayout"]["compilerCaseRevisions"]
        self.assertEqual(records[:-1], returned["stateLayout"]["compilerCaseRevisions"])
        self.assertNotEqual(self.layout.fingerprint(records[-1]),
                            self.layout.fingerprint(self.after["stateLayout"]["compilerCaseRevisions"][-1]))
        self.layout.validate_transition(self.before, restored)

    def test_an_unrelated_client_epoch_cannot_reuse_an_older_checker_revision(self):
        other_path = "app/consulta.cpp"
        source = self.layout._compiler_client_source(self.block(self.after), other_path)
        other = self.layout.revise_compiler_client_cases(
            self.after, self.owner, {"path": other_path, "source": source + "\n// Otro cliente.\n"},
            self.cases(self.before), case_semantics=self.semantics(self.before))
        self.layout.validate_transition(self.before, other)
        with self.assertRaises(self.layout.LayoutError):
            self.layout.revise_compiler_cases(other, self.owner, self.cases(self.after),
                                              case_semantics=self.semantics(self.after))

    def test_current_epoch_cannot_be_substituted_by_a_cmake_recipe(self):
        with self.assertRaises(self.layout.LayoutError):
            self.layout.revise_compiler_cmake_recipe(
                self.after, self.owner,
                {"path": "CMakeLists.txt",
                 "source": self.layout._compiler_client_source(
                     self.block(self.after), "CMakeLists.txt") + "\n# Otra receta.\n"})

    def test_forged_client_or_lost_history_and_workflow_are_rejected(self):
        for edit in (
            lambda document: document["stateLayout"]["compilerCaseRevisions"][-1].pop("client"),
            lambda document: document["stateLayout"]["compilerCaseRevisions"][-1]["client"].update(
                source="int main() { return 0; }\n"),
            lambda document: document["stateLayout"]["compilerCaseRevisions"].clear(),
            lambda document: document["stateLayout"]["editorial"].pop(),
            lambda document: document.pop("compilerWorkflowChecks"),
        ):
            document = deepcopy(self.after)
            edit(document)
            with self.assertRaises(self.layout.LayoutError):
                self.layout.validate_transition(self.before, document)


if __name__ == "__main__":
    unittest.main()
