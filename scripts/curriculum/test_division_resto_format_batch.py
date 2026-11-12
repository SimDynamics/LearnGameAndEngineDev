"""Regresion U4L2: division, resto y lectura preservan las fuentes historicas."""
from copy import deepcopy
import json
from pathlib import Path
import subprocess
import tempfile
import unittest

import stable_state_layout as state
from code_format_compat import require_source_format

ROOT = Path(__file__).resolve().parents[2]
LESSON = ROOT / "app/src/main/assets/content/es/programming/cpp_fundamentals/expresiones_y_decisiones/lesson_division_y_resto.json"
POSITIONS = ((0, 2), (0, 4), (1, 3), (2, 1), (2, 3),
             (3, 3), (3, 8), (4, 3), (5, 2))


def source_field(block):
    return "initialCode" if block["type"] == "cpp_compiler" else (
        "template" if block["type"] == "inline_fill_code" else "code")


def compilable_code(block):
    code = block[source_field(block)]
    if block["type"] == "inline_fill_code":
        for answer in block["answers"]:
            assert "[]" in code
            code = code.replace("[]", answer, 1)
        assert "[]" not in code
    return code


class DivisionRestoRealFormat(unittest.TestCase):
    def setUp(self):
        self.after = json.loads(LESSON.read_text(encoding="utf-8"))
        self.meta = self.after["stateLayout"]
        self.archived = {row["id"]: row["block"] for row in
                         self.meta["sourceFormatRevisions"]}

    def test_proveniencia_y_estado_historico_por_bloque(self):
        state.validate(self.after)
        self.assertEqual(len(POSITIONS), len(self.archived))
        self.assertEqual([self.meta["current"][p]["blocks"][b] for p,b in POSITIONS],
                         [edge["id"] for edge in self.meta["editorial"][-len(POSITIONS):]])
        before = deepcopy(self.after)
        before["stateLayout"].pop("sourceFormatRevisions")
        before["stateLayout"]["editorial"] = before["stateLayout"]["editorial"][:-len(POSITIONS)]
        for p,b in POSITIONS:
            identity = self.meta["current"][p]["blocks"][b]
            old, new = self.archived[identity], self.after["pages"][p]["blocks"][b]
            self.assertTrue(require_source_format(old, new), identity)
            field = source_field(old)
            self.assertEqual([x for x in old[field].splitlines() if x.strip()],
                             [x for x in new[field].splitlines() if x.strip()])
            before["pages"][p]["blocks"][b] = deepcopy(old)
        state.validate(before)
        state.validate_transition(before, self.after)
        self.assertEqual(state.validate(before), state.validate(self.after))
        for key in ("origin", "family", "current", "additions"):
            self.assertEqual(before["stateLayout"][key], self.meta[key])

    def test_cpp23_mismas_salidas_y_oraculos_publicados(self):
        with tempfile.TemporaryDirectory(prefix="u4l2-formato-") as folder:
            for i, (p,b) in enumerate(POSITIONS):
                identity = self.meta["current"][p]["blocks"][b]
                old, new = self.archived[identity], self.after["pages"][p]["blocks"][b]
                cases = new.get("outputCheck", {}).get("cases", [])
                inputs = [case["standardInput"] for case in cases] or [
                    new.get("initialStandardInput", "")]
                observations = []
                for label, block in (("antes",old),("despues",new)):
                    src = Path(folder) / f"{i}-{label}.cpp"
                    exe = Path(folder) / f"{i}-{label}"
                    src.write_text(compilable_code(block), encoding="utf-8")
                    built = subprocess.run(
                        ["c++", "-std=c++23", "-O0", "-o", str(exe), str(src)],
                        capture_output=True, text=True, timeout=90)
                    self.assertEqual(0, built.returncode, identity + built.stderr)
                    runs = []
                    for stdin in inputs:
                        run = subprocess.run([str(exe)], input=stdin,
                                             capture_output=True, text=True, timeout=15)
                        self.assertEqual(0, run.returncode, identity)
                        self.assertEqual("", run.stderr, identity)
                        runs.append((run.returncode, run.stdout, run.stderr))
                    observations.append(runs)
                self.assertEqual(observations[0], observations[1], identity)
                for result, case in zip(observations[1], cases):
                    self.assertIn(result[1], case["outputs"], identity)
                if new["type"] == "output_prediction":
                    self.assertIn(observations[1][0][1], new["answers"], identity)


if __name__ == "__main__":
    unittest.main()
