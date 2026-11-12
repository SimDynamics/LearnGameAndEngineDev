"""Regresión de for y límites: separar fases sin alterar resultados ni retos."""
from copy import deepcopy
import json
from pathlib import Path
import subprocess
import tempfile
import unittest

import stable_state_layout as state
from code_format_compat import require_source_format

ROOT = Path(__file__).resolve().parents[2]
LESSON = ROOT / "app/src/main/assets/content/es/programming/cpp_fundamentals/repeticion_y_terminacion/lesson_for_y_limites_del_recorrido.json"
POSITIONS = ((0, 2), (0, 4), (1, 3), (1, 4), (1, 5),
             (2, 1), (2, 2), (3, 3), (3, 4), (4, 3),
             (4, 4), (5, 1), (5, 3), (5, 6), (7, 2))


def source_field(block):
    if block["type"] == "cpp_compiler":
        return "initialCode"
    if block["type"] == "inline_fill_code":
        return "template"
    return "code"


def compilable_source(block):
    source = block[source_field(block)]
    if block["type"] == "fill_code":
        assert source.count("___") == 1
        source = source.replace("___", block["answers"][0], 1)
    if block["type"] == "inline_fill_code":
        for answer in block["answers"]:
            assert "[]" in source
            source = source.replace("[]", answer, 1)
        assert "[]" not in source
    return source


class ForLimitsFormatBatch(unittest.TestCase):
    def setUp(self):
        self.after = json.loads(LESSON.read_text(encoding="utf-8"))
        self.meta = self.after["stateLayout"]
        self.archives = {row["id"]: row["block"]
                         for row in self.meta["sourceFormatRevisions"]}

    def test_originals_and_order_separators_preserve_historical_state(self):
        state.validate(self.after)
        self.assertEqual(len(POSITIONS), len(self.archives))
        ids = [self.meta["current"][p]["blocks"][b] for p,b in POSITIONS]
        self.assertEqual("orden_while_puestos_2_3_4", self.meta["editorial"][-1]["id"])
        self.assertEqual(ids, [e["id"] for e in self.meta["editorial"][-len(POSITIONS)-1:-1]])
        old = deepcopy(self.after)
        old["stateLayout"].pop("sourceFormatRevisions")
        # Reconstruct the state before the physical-source edits, retaining
        # the independently authenticated, later order-card refinement.
        old["stateLayout"]["editorial"] = (
            old["stateLayout"]["editorial"][:-len(POSITIONS)-1] +
            old["stateLayout"]["editorial"][-1:])
        original_order = deepcopy(self.after["pages"][5]["blocks"][7])
        for p,b in POSITIONS:
            ident = self.meta["current"][p]["blocks"][b]
            before, after = self.archives[ident], self.after["pages"][p]["blocks"][b]
            self.assertTrue(require_source_format(before, after), ident)
            source = source_field(before)
            self.assertEqual(
                [line for line in before[source].splitlines() if line.strip()],
                [line for line in after[source].splitlines() if line.strip()], ident)
            old["pages"][p]["blocks"][b] = deepcopy(before)
        self.assertEqual(old["orderVisualSeparators"], self.after["orderVisualSeparators"])
        self.assertEqual(old["pages"][5]["blocks"][7], original_order)
        state.validate(old)
        state.validate_transition(old, self.after)
        self.assertEqual(state.validate(old), state.validate(self.after))
        for field in ("origin", "family", "current", "additions"):
            self.assertEqual(old["stateLayout"][field], self.meta[field])

    def test_cpp23_sources_and_student_challenges_behave_identically(self):
        with tempfile.TemporaryDirectory(prefix="for-limits-format-") as directory:
            for idx,(p,b) in enumerate(POSITIONS):
                identity = self.meta["current"][p]["blocks"][b]
                old, new = self.archives[identity], self.after["pages"][p]["blocks"][b]
                cases = new.get("outputCheck", {}).get("cases", [])
                inputs = [case["standardInput"] for case in cases] or [
                    new.get("initialStandardInput", "")]
                histories = []
                for label, block in (("antes", old), ("actual", new)):
                    src = Path(directory) / f"{idx}-{label}.cpp"
                    binary = Path(directory) / f"{idx}-{label}"
                    src.write_text(compilable_source(block), encoding="utf-8")
                    compilation = subprocess.run(
                        ["c++","-std=c++23","-O0","-o",str(binary),str(src)],
                        capture_output=True,text=True,timeout=90)
                    self.assertEqual(0, compilation.returncode, identity + compilation.stderr)
                    results = []
                    for standard_input in inputs:
                        run = subprocess.run([str(binary)], input=standard_input,
                                             capture_output=True,text=True,timeout=15)
                        self.assertEqual(0, run.returncode, identity)
                        self.assertEqual("", run.stderr, identity)
                        results.append((run.returncode,run.stdout,run.stderr))
                    histories.append(results)
                self.assertEqual(histories[0], histories[1], identity)
                if new["type"] == "output_prediction":
                    self.assertIn(histories[1][0][1],new["answers"],identity)
                # El primer editor deja un reto deliberado (i = 1 en lugar de i = 2).
                if (p,b) == (5,3):
                    self.assertNotIn(histories[1][0][1],cases[0]["outputs"])
                elif cases:
                    for result,case in zip(histories[1],cases):
                        self.assertIn(result[1],case["outputs"],identity)


if __name__ == "__main__":
    unittest.main()
