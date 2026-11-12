"""Regresion U4L7: if independientes, cadenas else-if y anidacion en C++23."""
from copy import deepcopy
import json
from pathlib import Path
import subprocess
import tempfile
import unittest

import stable_state_layout as state
from code_format_compat import require_source_format

ROOT = Path(__file__).resolve().parents[2]
PATH = ROOT / "app/src/main/assets/content/es/programming/cpp_fundamentals/expresiones_y_decisiones/lesson_decisiones_compuestas_y_pruebas.json"
POSITIONS = ((0, 2), (0, 4), (1, 2), (2, 2), (3, 3),
             (3, 8), (4, 3), (5, 1), (6, 3), (7, 2))


def source_field(block):
    return "initialCode" if block["type"] == "cpp_compiler" else "code"


class DecisionesCompuestasFormatoReal(unittest.TestCase):
    def setUp(self):
        self.current = json.loads(PATH.read_text(encoding="utf-8"))
        self.meta = self.current["stateLayout"]
        self.archives = {item["id"]: item["block"] for item in
                         self.meta["sourceFormatRevisions"]}

    def test_transicion_y_conservacion_de_cadenas_independientes(self):
        state.validate(self.current)
        self.assertEqual(len(POSITIONS), len(self.archives))
        ids = [self.meta["current"][p]["blocks"][b] for p, b in POSITIONS]
        self.assertEqual(ids, [e["id"] for e in self.meta["editorial"][-len(POSITIONS):]])
        historical = deepcopy(self.current)
        historical["stateLayout"].pop("sourceFormatRevisions")
        historical["stateLayout"]["editorial"] = historical["stateLayout"]["editorial"][:-len(POSITIONS)]
        for p, b in POSITIONS:
            identity = self.meta["current"][p]["blocks"][b]
            before, after = self.archives[identity], self.current["pages"][p]["blocks"][b]
            self.assertTrue(require_source_format(before, after), identity)
            slot = source_field(before)
            self.assertEqual(
                [line for line in before[slot].splitlines() if line.strip()],
                [line for line in after[slot].splitlines() if line.strip()], identity)
            self.assertEqual(before[slot].count("} else if ("),
                             after[slot].count("} else if ("), identity)
            self.assertEqual(before[slot].count("} else {"),
                             after[slot].count("} else {"), identity)
            historical["pages"][p]["blocks"][b] = deepcopy(before)
        # Dos if independientes, que no forman una unica cadena.
        p, b = 1, 2
        self.assertIn("    }\n\n    if (aciertos >= 8)",
                      self.current["pages"][p]["blocks"][b]["code"])
        state.validate(historical)
        state.validate_transition(historical, self.current)
        self.assertEqual(state.validate(historical), state.validate(self.current))
        for key in ("origin", "family", "current", "additions"):
            self.assertEqual(historical["stateLayout"][key], self.meta[key])

    def test_cxx23_programas_y_casos_no_cambian(self):
        with tempfile.TemporaryDirectory(prefix="lgaed-u4l7-formato-") as directory:
            for index, (p, b) in enumerate(POSITIONS):
                identity = self.meta["current"][p]["blocks"][b]
                before, after = self.archives[identity], self.current["pages"][p]["blocks"][b]
                slot = source_field(before)
                inputs = [case["standardInput"] for case in
                          after.get("outputCheck", {}).get("cases", [])]
                if not inputs:
                    inputs = [after.get("initialStandardInput", "")]
                programs = []
                for label, block in (("anterior", before), ("actual", after)):
                    code = block[slot]
                    if block["type"] == "fill_code":
                        self.assertEqual(1, code.count("___"))
                        code = code.replace("___", "&&")
                    source = Path(directory) / f"{index}-{label}.cpp"
                    binary = Path(directory) / f"{index}-{label}"
                    source.write_text(code, encoding="utf-8")
                    built = subprocess.run(
                        ["c++", "-std=c++23", "-O0", "-o", str(binary), str(source)],
                        capture_output=True, text=True, timeout=90)
                    self.assertEqual(0, built.returncode, f"{identity}: {built.stderr}")
                    cases = []
                    for stdin in inputs:
                        result = subprocess.run([str(binary)], input=stdin,
                                                capture_output=True, text=True, timeout=15)
                        self.assertEqual(0, result.returncode, identity)
                        self.assertEqual("", result.stderr, identity)
                        cases.append((result.returncode, result.stdout, result.stderr))
                    programs.append(cases)
                self.assertEqual(programs[0], programs[1], identity)
                for result, case in zip(programs[1],
                                        after.get("outputCheck", {}).get("cases", [])):
                    self.assertIn(result[1], case["outputs"], identity)


if __name__ == "__main__":
    unittest.main()
