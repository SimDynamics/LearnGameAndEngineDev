"""Output checking remains optional and strictly separate from program execution."""

import copy
import unittest

import validar_json_programacion as contract


class CppOutputCheckContractTest(unittest.TestCase):
    def setUp(self):
        self.block = {"type": "cpp_compiler", "language": "cpp", "initialCode": "int main() {}"}
        self.check = {"cases": [{"standardInput": "", "outputs": ["Ready", "Ready\n"],
                                "explanation": "El programa produce el mensaje solicitado.",
                                "reviewFeedback": [{"outputs": ["Wrong", "Wrong\n"],
                                                    "text": "Revisa el mensaje."}]}]}

    def errors(self, check):
        contract.ERRORS.clear()
        contract.validate_cpp_compiler(dict(self.block, outputCheck=check), "fixture")
        return list(contract.ERRORS)

    def test_existing_compilers_require_no_new_fields_and_preserve_their_source(self):
        contract.ERRORS.clear()
        contract.validate_cpp_compiler(self.block, "legacy")
        self.assertEqual([], contract.ERRORS)
        current = dict(self.block, outputCheck=self.check)
        previous = copy.deepcopy(current)
        self.assertEqual([], self.errors(self.check))
        self.assertEqual(previous, current)
        self.assertIn("outputCheck", contract.FIELDS["cpp_compiler"])
        self.assertTrue(contract.preserves_previous_compiler("fixture.json", 0, 0, self.block, current, {}))
        self.assertFalse(contract.preserves_previous_compiler(
            "fixture.json", 0, 0, self.block, dict(current, initialCode="different"), {}))
        changed = copy.deepcopy(current)
        changed["outputCheck"]["cases"][0]["outputs"] = ["different"]
        self.assertFalse(contract.preserves_previous_compiler("fixture.json", 0, 0, current, changed, {}))

    def test_empty_output_and_explicit_final_newline_aliases_remain_distinct(self):
        for values in ([""], ["\n"], ["A"], ["A\n"], ["A", "A\n"], [" A ", "A"], ["\t", " "]):
            with self.subTest(values=values):
                self.assertEqual([], self.errors({"cases": [{"standardInput": "", "outputs": values}]}))
        self.assertNotEqual({"A"}, contract.output_check_outputs(["A\n"], "fixture"))

    def test_defaults_exact_and_token_matching_uses_only_ascii_whitespace(self):
        cases = [{"standardInput": value, "outputs": ["Ready"]} for value in ("A  B", "A\tB")]
        self.assertEqual([], self.errors({"cases": cases}))
        self.assertEqual([], self.errors({"inputComparison": "exact", "cases": cases}))
        self.assertTrue(self.errors({"inputComparison": "tokens", "cases": cases}))
        self.assertEqual(("A", "B"), contract.normalize_output_check_input(" \tA\r\nB\f\v", "tokens"))
        self.assertEqual(("A\u00a0B",), contract.normalize_output_check_input("A\u00a0B", "tokens"))
        self.assertEqual(("A\u2003B",), contract.normalize_output_check_input("A\u2003B", "tokens"))
        self.assertEqual([], self.errors({"inputComparison": "tokens", "cases": [
            {"standardInput": "A\u00a0B", "outputs": ["x"]},
            {"standardInput": "A B", "outputs": ["x"]}]}))

    def test_duplicate_cases_are_rejected_under_the_selected_input_policy(self):
        for mode in ("exact", "tokens"):
            self.assertTrue(self.errors({"inputComparison": mode, "cases": [
                {"standardInput": "A", "outputs": ["one"]},
                {"standardInput": "A", "outputs": ["two"]}]}))
        for value in (" ", "\t", "\r\n", "\f\v"):
            self.assertTrue(self.errors({"inputComparison": "tokens", "cases": [
                {"standardInput": "", "outputs": ["one"]},
                {"standardInput": value, "outputs": ["two"]}]}))

    def test_malformed_shapes_and_unknown_fields_are_not_accepted(self):
        for check in (None, False, [], "x", {}, {"cases": []}, {"cases": {}}, {"cases": [None]},
                      dict(self.check, unknown=True), dict(self.check, inputComparison="trimmed"),
                      dict(self.check, inputComparison=None), dict(self.check, inputComparison=[])):
            with self.subTest(check=check):
                self.assertTrue(self.errors(check))
        for patch in ({"standardInput": None}, {"standardInput": []}, {"outputs": []},
                      {"outputs": None}, {"outputs": "Ready"}, {"outputs": [False]},
                      {"outputs": ["a", "a"]}, {"other": "x"}):
            changed = copy.deepcopy(self.check)
            changed["cases"][0].update(patch)
            with self.subTest(patch=patch):
                self.assertTrue(self.errors(changed))
        for removed in ("standardInput", "outputs"):
            changed = copy.deepcopy(self.check)
            del changed["cases"][0][removed]
            self.assertTrue(self.errors(changed))

    def test_feedback_is_optional_but_present_explanations_and_guides_are_nonblank(self):
        plain = {"cases": [{"standardInput": "", "outputs": ["Ready"]}]}
        self.assertEqual([], self.errors(plain))
        plain["cases"][0]["reviewFeedback"] = []
        self.assertEqual([], self.errors(plain))
        for text in (None, False, [], {}, "", " \t\n"):
            changed = copy.deepcopy(self.check)
            changed["cases"][0]["explanation"] = text
            self.assertTrue(self.errors(changed))
            changed = copy.deepcopy(self.check)
            changed["cases"][0]["reviewFeedback"][0]["text"] = text
            self.assertTrue(self.errors(changed))
        for value in (None, False, {}, "text", [None], [{"outputs": ["wrong"]}],
                      [{"outputs": [], "text": "Review"}],
                      [{"outputs": ["wrong"], "text": "Review", "extra": 1}]):
            changed = copy.deepcopy(self.check)
            changed["cases"][0]["reviewFeedback"] = value
            self.assertTrue(self.errors(changed))

    def test_guide_outputs_cannot_ambiguously_overlap_success_or_each_other(self):
        for values in (["Ready"], ["Ready\n"], ["Wrong"], ["Wrong\n"]):
            changed = copy.deepcopy(self.check)
            changed["cases"][0]["reviewFeedback"].append({"outputs": values, "text": "Another guide"})
            self.assertTrue(self.errors(changed))
        # Different inputs own different oracles; do not apply a global output rule.
        changed = copy.deepcopy(self.check)
        changed["cases"].append({"standardInput": "other", "outputs": ["Wrong"]})
        self.assertEqual([], self.errors(changed))

    def cmake_errors(self, cases, mode="exact"):
        contract.ERRORS.clear()
        contract.validate_cpp_output_check_contract({
            "cmake": {"target": "client"}, "outputCheck": {"inputComparison": mode, "cases": cases}}, "cmake")
        return list(contract.ERRORS)

    def test_targets_can_share_input_without_sharing_their_oracles(self):
        cases = [{"target": "client", "standardInput": "", "outputs": ["1:2"]},
                 {"target": "battery", "standardInput": "", "outputs": ["Bateria: OK\n"]}]
        original = copy.deepcopy(cases)
        self.assertEqual([], self.cmake_errors(cases))
        self.assertEqual(original, cases)
        self.assertTrue(self.errors({"cases": cases}))  # No CMake execution identity in a plain compiler.
        cases[1]["standardInput"] = " \t\n"
        self.assertEqual([], self.cmake_errors(cases, "tokens"))

    def test_target_or_global_cases_reject_only_real_ambiguities(self):
        case = {"target": "client", "standardInput": "A B", "outputs": ["OK"]}
        for other in (dict(case), dict(case, outputs=["Other"]),
                      {"standardInput": "A B", "outputs": ["Global"]}):
            for cases in ([case, other], [other, case]):
                self.assertTrue(self.cmake_errors(cases))
        self.assertTrue(self.cmake_errors([case, dict(case, standardInput="A\nB")], "tokens"))
        self.assertEqual([], self.cmake_errors([case, dict(case, target="other")], "tokens"))
        self.assertEqual([], self.cmake_errors([case, {"standardInput": "different", "outputs": ["Global"]}]))

    def test_target_uses_the_existing_cmake_identifier_contract(self):
        for target in (None, False, [], {}, "", " client", "client ", "../client", "client;other", "a/b",
                       "9client", "a" * 129):
            with self.subTest(target=target):
                self.assertTrue(self.cmake_errors([{"target": target, "standardInput": "", "outputs": ["OK"]}]))
        for target in ("client", "_a", "client.test-2+debug", "a" * 128):
            self.assertEqual([], self.cmake_errors([{"target": target, "standardInput": "", "outputs": ["OK"]}]))

    def test_legacy_cmake_checks_need_no_target_or_content_migration(self):
        self.assertEqual([], self.cmake_errors(self.check["cases"]))


if __name__ == "__main__":
    unittest.main()
