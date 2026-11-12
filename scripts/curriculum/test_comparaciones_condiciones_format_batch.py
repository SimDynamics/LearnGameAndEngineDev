"""Regresion U4L3: comparaciones, estado y salida conservan historia y oraculos."""
from copy import deepcopy
import json
from pathlib import Path
import subprocess
import tempfile
import unittest

import stable_state_layout as state
from code_format_compat import require_source_format

ROOT = Path(__file__).resolve().parents[2]
LESSON = ROOT / "app/src/main/assets/content/es/programming/cpp_fundamentals/expresiones_y_decisiones/lesson_comparaciones_y_condiciones.json"
POSITIONS = ((0, 2), (0, 4), (1, 3), (2, 2), (2, 4),
             (2, 5), (3, 3), (3, 8), (4, 1), (5, 2))


def source_field(block):
    if block["type"] == "inline_fill_code":
        return "template"
    return "initialCode" if block["type"] == "cpp_compiler" else "code"


def compilable_source(block):
    source = block[source_field(block)]
    if block["type"] == "fill_code":
        self_answer = block["answers"][0]
        assert source.count("___") == 1
        source = source.replace("___", self_answer)
    if block["type"] == "inline_fill_code":
        for answer in block["answers"]:
            assert "[]" in source
            source = source.replace("[]", answer, 1)
        assert "[]" not in source
    return source


class ComparacionesFormatoReal(unittest.TestCase):
    def setUp(self):
        self.current = json.loads(LESSON.read_text(encoding="utf-8"))
        self.meta = self.current["stateLayout"]
        self.archives = {entry["id"]: entry["block"] for entry in
                         self.meta["sourceFormatRevisions"]}

    def test_estado_v2_y_procedencia_sin_reescribir_respuestas(self):
        state.validate(self.current)
        self.assertEqual(len(POSITIONS), len(self.archives))
        self.assertEqual([self.meta["current"][p]["blocks"][b] for p,b in POSITIONS],
                         [edge["id"] for edge in self.meta["editorial"][-len(POSITIONS):]])
        original = deepcopy(self.current)
        original["stateLayout"].pop("sourceFormatRevisions")
        original["stateLayout"]["editorial"] = original["stateLayout"]["editorial"][:-len(POSITIONS)]
        for p,b in POSITIONS:
            identity = self.meta["current"][p]["blocks"][b]
            before = self.archives[identity]
            after = self.current["pages"][p]["blocks"][b]
            self.assertTrue(require_source_format(before, after), identity)
            key = source_field(before)
            self.assertEqual([line for line in before[key].splitlines() if line.strip()],
                             [line for line in after[key].splitlines() if line.strip()], identity)
            original["pages"][p]["blocks"][b] = deepcopy(before)
        state.validate(original)
        state.validate_transition(original, self.current)
        self.assertEqual(state.validate(original), state.validate(self.current))
        for field in ("origin", "family", "current", "additions"):
            self.assertEqual(original["stateLayout"][field], self.meta[field])

    def test_cpp23_compilaciones_anteriores_y_nuevas_equivalentes(self):
        with tempfile.TemporaryDirectory(prefix="u4l3-formato-") as temp:
            for i,(p,b) in enumerate(POSITIONS):
                identity = self.meta["current"][p]["blocks"][b]
                before, after = self.archives[identity], self.current["pages"][p]["blocks"][b]
                cases = after.get("outputCheck", {}).get("cases", [])
                inputs = [case["standardInput"] for case in cases] or [
                    after.get("initialStandardInput", "")]
                histories = []
                for label,source_block in (("antes",before),("despues",after)):
                    code = compilable_source(source_block)
                    src = Path(temp) / f"{i}-{label}.cpp"
                    exe = Path(temp) / f"{i}-{label}"
                    src.write_text(code, encoding="utf-8")
                    built = subprocess.run(
                        ["c++", "-std=c++23", "-O0", "-o", str(exe), str(src)],
                        capture_output=True, text=True, timeout=90)
                    self.assertEqual(0, built.returncode, identity + built.stderr)
                    obs=[]
                    for stdin in inputs:
                        run = subprocess.run([str(exe)], input=stdin,
                                             capture_output=True, text=True, timeout=15)
                        self.assertEqual(0, run.returncode, identity)
                        self.assertEqual("", run.stderr, identity)
                        obs.append((run.returncode, run.stdout, run.stderr))
                    histories.append(obs)
                self.assertEqual(histories[0], histories[1], identity)
                for observation,case in zip(histories[1], cases):
                    self.assertIn(observation[1], case["outputs"], identity)
                if after["type"] == "output_prediction":
                    self.assertIn(histories[1][0][1], after["answers"], identity)


if __name__ == "__main__":
    unittest.main()
