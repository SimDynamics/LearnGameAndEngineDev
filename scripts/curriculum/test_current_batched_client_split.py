"""Original U6L19 batch versus independent one-input Windows C++ sessions.

Only in-memory authoring; never rewrites Work JSON, student drafts or progress.
"""
from copy import deepcopy
import json
from pathlib import Path
import unittest

import stable_state_layout as layout

ROOT = Path(__file__).resolve().parents[2]
REL = ("programming/cpp_fundamentals/funciones_y_contratos/"
       "lesson_interfaces_cabeceras_y_programas_con_varios_archivos.json")
ASSETS = ROOT / "app/src/main/assets/content"
OWNER = "capacidad_veinte_bateria"
AMOUNTS = (0, 12, 19, 20, 21, 24, 25, 120)
CLIENT = (
    '#include "sobres.h"\n'
    '#include <iostream>\n'
    '\n'
    'int main() {\n'
    '    int tarjetas{};\n'
    '    if (!(std::cin >> tarjetas)) return 1;\n'
    '    std::cout << "Tarjetas: " << tarjetas << "; "\n'
    '              << descripcion_sobres(tarjetas)\n'
    '              << ": " << sobres_necesarios(tarjetas);\n'
    '    return 0;\n'
    '}\n'
)


class CurrentBatchSplit(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.path = ASSETS / "es" / REL
        cls.bytes = cls.path.read_bytes()
        cls.before = json.loads(cls.bytes)
        cls.family = {
            locale: json.loads((ASSETS / locale / REL).read_bytes())
            for locale in cls.before["stateLayout"]["family"]
        }
        cls.owner = layout._blocks(cls.before)[OWNER]

    def case_list(self):
        old = self.owner["outputCheck"]["cases"]
        self.assertEqual(len(old), 1)
        self.assertEqual(old[0]["standardInput"], "")
        lines = old[0]["outputs"][0].split("\n")
        self.assertEqual(len(lines), len(AMOUNTS))
        old_main = next(file["initialCode"] for file in self.owner["projectFiles"]
                        if file["path"] == "main.cpp")
        self.assertEqual(old_main, self.owner["initialCode"])
        for amount, line in zip(AMOUNTS, lines, strict=True):
            self.assertTrue(line.startswith(f"Tarjetas: {amount}; "))
            self.assertEqual(old_main.count(f"descripcion_sobres({amount})"), 1)
            self.assertEqual(old_main.count(f"sobres_necesarios({amount})"), 1)
        return [
            {"standardInput": f"{amount}\n", "outputs": [line, line + "\n"],
             "exitCode": 0}
            for amount, line in zip(AMOUNTS, lines, strict=True)
        ]

    def reviewed(self):
        return layout.revise_compiler_client_cases(
            self.before, OWNER, {"path": "main.cpp", "source": CLIENT},
            self.case_list(), case_semantics=[])

    def test_eight_separate_cases_keep_history_and_stable_owner(self):
        after = self.reviewed()
        layout.validate_transition(self.before, after)
        self.assertEqual(layout.validate(self.before), layout.validate(after))
        original = self.before["stateLayout"]
        revised = after["stateLayout"]
        for field in ("origin", "current", "family", "additions",
                      "sourceFormatRevisions", "retirementHistory"):
            self.assertEqual(original.get(field), revised.get(field))
        self.assertEqual(revised["editorial"][:len(original["editorial"])],
                         original["editorial"])
        self.assertEqual(len(revised["editorial"]), len(original["editorial"]) + 1)
        previous = original.get("compilerCaseRevisions", [])
        journal = revised["compilerCaseRevisions"]
        self.assertEqual(journal[:len(previous)], previous)
        self.assertEqual(len(journal), len(previous) + 1)
        last = journal[-1]
        self.assertEqual(last["id"], OWNER)
        self.assertEqual(last["block"], self.owner)
        self.assertEqual(last["client"], {"path": "main.cpp", "source": CLIENT})
        previous_marks = [field for field in self.before["textSemantics"]["fields"]
                          if field["block"] == OWNER and
                          field["path"].startswith("/outputCheck/cases/")]
        self.assertEqual(last["semanticBefore"], previous_marks)
        self.assertEqual(last["semanticAfter"], [])
        block = layout._blocks(after)[OWNER]
        self.assertEqual(block["initialCode"], CLIENT)
        for old, new in zip(self.owner["projectFiles"],
                            block["projectFiles"], strict=True):
            self.assertEqual(new["path"], old["path"])
            self.assertEqual(new["initialCode"],
                             CLIENT if old["path"] == "main.cpp" else old["initialCode"])
        self.assertEqual(block["sourceFiles"], self.owner["sourceFiles"])
        self.assertEqual(block["outputCheck"]["cases"], self.case_list())
        self.assertEqual([f for f in after["textSemantics"]["fields"]
                          if f["block"] != OWNER],
                         [f for f in self.before["textSemantics"]["fields"]
                          if f["block"] != OWNER])
        self.assertFalse(any(f["block"] == OWNER
                             for f in after["textSemantics"]["fields"]))
        self.assertEqual(self.path.read_bytes(), self.bytes)

    def test_unverified_cases_and_source_changes_are_not_allowed(self):
        valid = self.reviewed()
        for mode in ("oracle", "client", "other-source"):
            with self.subTest(mode=mode):
                wrong = deepcopy(valid)
                current = layout._blocks(wrong)[OWNER]
                if mode == "oracle":
                    current["outputCheck"]["cases"][0]["outputs"][0] += " falso"
                elif mode == "client":
                    wrong["stateLayout"]["compilerCaseRevisions"][-1]["client"]["source"] += " "
                else:
                    current["projectFiles"][0]["initialCode"] += "// no autorizado"
                with self.assertRaises(layout.LayoutError):
                    layout.validate_transition(self.before, wrong)
        self.assertEqual(self.path.read_bytes(), self.bytes)


if __name__ == "__main__":
    unittest.main()
