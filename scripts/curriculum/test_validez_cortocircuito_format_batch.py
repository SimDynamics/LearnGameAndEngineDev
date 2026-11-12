"""Regresion U4L6: cortocircuito y division conservan logica, salida e identidad."""
from copy import deepcopy
import json
from pathlib import Path
import subprocess
import tempfile
import unittest

import stable_state_layout as state
from code_format_compat import require_source_format

ROOT = Path(__file__).resolve().parents[2]
PATH = ROOT / "app/src/main/assets/content/es/programming/cpp_fundamentals/expresiones_y_decisiones/lesson_validez_y_cortocircuito.json"
POSITIONS = ((0, 2), (1, 2), (2, 2), (3, 2),
             (3, 7), (4, 2), (6, 2), (6, 6))


def source_field(block):
    return "initialCode" if block["type"] == "cpp_compiler" else "code"


class ValidezCortocircuitoFormatoReal(unittest.TestCase):
    def setUp(self):
        self.current = json.loads(PATH.read_text(encoding="utf-8"))
        self.meta = self.current["stateLayout"]
        self.archives = {item["id"]: item["block"] for item in
                         self.meta["sourceFormatRevisions"]}

    def test_archivos_procedencia_y_transicion_v2(self):
        state.validate(self.current)
        self.assertEqual(len(POSITIONS), len(self.archives))
        ids = [self.meta["current"][p]["blocks"][b] for p, b in POSITIONS]
        self.assertEqual(ids, [edge["id"] for edge in self.meta["editorial"][-len(POSITIONS):]])
        historical = deepcopy(self.current)
        historical["stateLayout"].pop("sourceFormatRevisions")
        historical["stateLayout"]["editorial"] = (
            historical["stateLayout"]["editorial"][:-len(POSITIONS)])
        for p, b in POSITIONS:
            identity = self.meta["current"][p]["blocks"][b]
            before, after = self.archives[identity], self.current["pages"][p]["blocks"][b]
            self.assertTrue(require_source_format(before, after), identity)
            field = source_field(before)
            self.assertEqual(
                [line for line in before[field].splitlines() if line.strip()],
                [line for line in after[field].splitlines() if line.strip()], identity)
            historical["pages"][p]["blocks"][b] = deepcopy(before)
        state.validate(historical)
        state.validate_transition(historical, self.current)
        self.assertEqual(state.validate(historical), state.validate(self.current))
        for field in ("origin", "family", "current", "additions"):
            self.assertEqual(historical["stateLayout"][field], self.meta[field])

    def test_cxx23_guardas_originales_y_fuentes_espaciadas_son_equivalentes(self):
        with tempfile.TemporaryDirectory(prefix="lgaed-u4l6-formato-") as directory:
            for index, (p, b) in enumerate(POSITIONS):
                identity = self.meta["current"][p]["blocks"][b]
                before, after = self.archives[identity], self.current["pages"][p]["blocks"][b]
                field = source_field(before)
                inputs = [case["standardInput"] for case in
                          after.get("outputCheck", {}).get("cases", [])]
                if not inputs:
                    inputs = [after.get("initialStandardInput", "")]
                histories = []
                for label, block in (("anterior", before), ("actual", after)):
                    code = block[field]
                    if block["type"] == "fill_code":
                        self.assertEqual(1, code.count("___"))
                        code = code.replace("___", ">")
                    source = Path(directory) / f"{index}-{label}.cpp"
                    binary = Path(directory) / f"{index}-{label}"
                    source.write_text(code, encoding="utf-8")
                    built = subprocess.run(
                        ["c++", "-std=c++23", "-O0", "-o", str(binary), str(source)],
                        capture_output=True, text=True, timeout=90)
                    self.assertEqual(0, built.returncode, f"{identity}: {built.stderr}")
                    runs = []
                    for stdin in inputs:
                        result = subprocess.run(
                            [str(binary)], input=stdin,
                            capture_output=True, text=True, timeout=15)
                        self.assertEqual(0, result.returncode, identity)
                        self.assertEqual("", result.stderr, identity)
                        runs.append((result.returncode, result.stdout, result.stderr))
                    histories.append(runs)
                self.assertEqual(histories[0], histories[1], identity)
                for run, case in zip(histories[1],
                                     after.get("outputCheck", {}).get("cases", [])):
                    self.assertIn(run[1], case["outputs"], identity)


if __name__ == "__main__":
    unittest.main()
