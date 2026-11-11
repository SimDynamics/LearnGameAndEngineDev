"""Verifica el nuevo objetivo de stderr exacto sin ampliar contratos antiguos."""
import copy
import unittest

import stable_state_layout as layout
import validar_json_programacion as contract


class ExactStandardErrorContractTest(unittest.TestCase):
    def setUp(self):
        self.original = {
            "type": "cpp_compiler", "language": "cpp",
            "initialCode": "int main() { return 1; }",
            "outputCheck": {"cases": [
                {"standardInput": "2x\n", "outputs": ["Validos=0\n"],
                 "exitCode": 1, "explanation": "Se rechazó el dato."},
            ]},
        }

    def errors(self, block):
        contract.ERRORS.clear()
        contract.validate_cpp_compiler(block, "test.stderr.exact")
        return list(contract.ERRORS)

    def upgraded(self, value="Linea=1:FORMATO\n"):
        copy_of = copy.deepcopy(self.original)
        copy_of["outputCheck"]["cases"][0]["standardErrorExact"] = value
        return copy_of

    def test_accepts_exact_bytes_including_empty_and_final_newline(self):
        for value in ("", "Linea=1:FORMATO\n", "Aviso\r\n", " \n",
                      "Diagnóstico: código=ñ\n"):
            with self.subTest(value=value):
                upgraded = self.upgraded(value)
                self.assertEqual([], self.errors(upgraded))
                self.assertEqual(value,
                    upgraded["outputCheck"]["cases"][0]["standardErrorExact"])

    def test_rejects_wrong_type_or_combined_nonblank_expectation(self):
        for value in (None, True, False, 0, [], {}, [""], {"exact": ""}):
            with self.subTest(value=value):
                self.assertTrue(self.errors(self.upgraded(value)))
        mixed = self.upgraded()
        mixed["outputCheck"]["cases"][0]["standardError"] = "nonBlank"
        self.assertTrue(self.errors(mixed))

    def test_historical_state_fingerprint_keeps_existing_slot(self):
        updated = self.upgraded()
        self.assertEqual(layout.state_fingerprint(self.original),
                         layout.state_fingerprint(updated))
        self.assertTrue(contract.preserves_previous_compiler(
            "fixture.json", 0, 0, self.original, updated, {}))
        self.assertFalse(contract.preserves_previous_compiler(
            "fixture.json", 0, 0, updated, self.original, {}))

    def test_strict_identity_and_previous_diagnostic_never_get_weak(self):
        original = self.upgraded()
        for change in (
            lambda c: c["outputCheck"]["cases"][0].update(
                standardErrorExact="Otro diagnóstico\n"),
            lambda c: c["outputCheck"]["cases"][0].pop("standardErrorExact"),
            lambda c: c["outputCheck"]["cases"][0].update(
                outputs=["Salida inventada"]),
            lambda c: c.update(initialCode="int main(){return 0;}"),
        ):
            altered = copy.deepcopy(original)
            change(altered)
            self.assertFalse(contract.preserves_previous_compiler(
                "fixture.json", 0, 0, original, altered, {}))
        old_nonblank = copy.deepcopy(self.original)
        old_nonblank["outputCheck"]["cases"][0]["standardError"] = "nonBlank"
        mixed = self.upgraded()
        self.assertFalse(contract.preserves_previous_compiler(
            "fixture.json", 0, 0, old_nonblank, mixed, {}))
        # An existing exact objective must never be weakened by a new
        # document that simply omits it, even if the state projection matches.
        before = {"block": copy.deepcopy(original)}
        after = {"block": copy.deepcopy(original)}
        after["block"]["outputCheck"]["cases"][0].pop("standardErrorExact")
        with self.assertRaisesRegex(layout.LayoutError, "exact stderr"):
            layout._preserve_compiler_channel_requirements(before, after)


if __name__ == "__main__":
    unittest.main()
