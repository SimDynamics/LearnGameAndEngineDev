"""Regresion U4L5: formato fisico y contratos historicos de combinaciones logicas."""
from copy import deepcopy
import json
from pathlib import Path
import subprocess
import tempfile
import unittest

import stable_state_layout as state
from code_format_compat import require_source_format

ROOT = Path(__file__).resolve().parents[2]
PATH = ROOT / "app/src/main/assets/content/es/programming/cpp_fundamentals/expresiones_y_decisiones/lesson_combinar_condiciones.json"
POSITIONS = ((0, 2), (0, 4), (1, 2), (1, 3), (2, 4),
             (3, 2), (3, 7), (4, 1), (5, 2), (5, 6))


def source_slot(block):
    if block["type"] == "cpp_compiler":
        return "initialCode"
    if block["type"] == "inline_fill_code":
        return "template"
    return "code"


def runnable(source, kind):
    # Solo para compilar las consignas incompletas; no cambia su JSON.
    if kind == "fill_code":
        assert source.count("___") == 1
        return source.replace("___", "&&")
    if kind == "inline_fill_code":
        assert source.count("[]") == 2
        return source.replace("[]", "&&", 1).replace("[]", "||", 1)
    return source


class CombinarCondicionesFormatoReal(unittest.TestCase):
    def setUp(self):
        self.current = json.loads(PATH.read_text(encoding="utf-8"))
        self.state = self.current["stateLayout"]
        self.archives = {item["id"]: item["block"] for item in
                         self.state["sourceFormatRevisions"]}

    def test_transicion_real_preserva_historia_respuestas_y_fuentes(self):
        state.validate(self.current)
        self.assertEqual(len(POSITIONS), len(self.archives))
        current_ids = [self.state["current"][p]["blocks"][b] for p, b in POSITIONS]
        self.assertEqual(current_ids, [e["id"] for e in self.state["editorial"][-len(POSITIONS):]])
        original = deepcopy(self.current)
        original["stateLayout"].pop("sourceFormatRevisions")
        original["stateLayout"]["editorial"] = original["stateLayout"]["editorial"][:-len(POSITIONS)]
        for p, b in POSITIONS:
            identity = self.state["current"][p]["blocks"][b]
            before = self.archives[identity]
            after = self.current["pages"][p]["blocks"][b]
            self.assertTrue(require_source_format(before, after))
            field = source_slot(before)
            self.assertEqual(
                [line for line in before[field].splitlines() if line.strip()],
                [line for line in after[field].splitlines() if line.strip()])
            self.assertIn("\n\n", after[field])
            original["pages"][p]["blocks"][b] = deepcopy(before)
        state.validate(original)
        state.validate_transition(original, self.current)
        self.assertEqual(state.validate(original), state.validate(self.current))
        for key in ("origin", "family", "current", "additions"):
            self.assertEqual(original["stateLayout"][key], self.state[key])

    def test_cxx23_compila_y_ejecuta_variantes_originales_y_legibles(self):
        with tempfile.TemporaryDirectory(prefix="lgaed-u4l5-format-") as directory:
            for index, (p, b) in enumerate(POSITIONS):
                identity = self.state["current"][p]["blocks"][b]
                before = self.archives[identity]
                after = self.current["pages"][p]["blocks"][b]
                field, kind = source_slot(before), before["type"]
                inputs = [case["standardInput"] for case in
                          after.get("outputCheck", {}).get("cases", [])]
                if not inputs:
                    inputs = [after.get("initialStandardInput", "")]
                programs = []
                for label, block in (("antes", before), ("despues", after)):
                    source = Path(directory) / f"{index}-{label}.cpp"
                    executable = Path(directory) / f"{index}-{label}"
                    source.write_text(runnable(block[field], kind), encoding="utf-8")
                    compile_result = subprocess.run(
                        ["c++", "-std=c++23", "-O0", "-o", str(executable), str(source)],
                        capture_output=True, text=True, timeout=90)
                    self.assertEqual(0, compile_result.returncode,
                                     f"{identity}: {compile_result.stderr}")
                    runs = []
                    for stdin in inputs:
                        run = subprocess.run([str(executable)], input=stdin,
                                             capture_output=True, text=True, timeout=15)
                        self.assertEqual(0, run.returncode, identity)
                        self.assertEqual("", run.stderr, identity)
                        runs.append((run.returncode, run.stdout, run.stderr))
                    programs.append(runs)
                self.assertEqual(programs[0], programs[1], identity)
                cases = after.get("outputCheck", {}).get("cases", [])
                for run, case in zip(programs[1], cases):
                    self.assertIn(run[1], case["outputs"], identity)


if __name__ == "__main__":
    unittest.main()
