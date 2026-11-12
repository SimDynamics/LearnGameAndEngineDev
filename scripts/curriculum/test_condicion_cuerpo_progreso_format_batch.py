"""Regresión de la primera lección de bucles: fuentes C++23 y estado histórico."""
from copy import deepcopy
import json
from pathlib import Path
import subprocess
import tempfile
import unittest

import stable_state_layout as state
from code_format_compat import require_source_format

ROOT = Path(__file__).resolve().parents[2]
LESSON = ROOT / "app/src/main/assets/content/es/programming/cpp_fundamentals/repeticion_y_terminacion/lesson_condicion_cuerpo_y_progreso.json"
POSITIONS = ((0, 3), (0, 5), (1, 4), (1, 5), (2, 3), (2, 4),
             (3, 4), (4, 2), (4, 6), (5, 2), (5, 6))


def source_field(block):
    return "initialCode" if block["type"] == "cpp_compiler" else "code"


def ready_source(block):
    source = block[source_field(block)]
    if block["type"] == "fill_code":
        assert source.count("___") == 1
        source = source.replace("___", block["answers"][0], 1)
    return source


class WhileConditionFormatBatch(unittest.TestCase):
    def setUp(self):
        self.after = json.loads(LESSON.read_text(encoding="utf-8"))
        self.layout = self.after["stateLayout"]
        self.archives = {item["id"]: item["block"]
                         for item in self.layout["sourceFormatRevisions"]}

    def test_all_originals_and_historical_responses_survive(self):
        state.validate(self.after)
        self.assertEqual(len(POSITIONS), len(self.archives))
        ids = [self.layout["current"][p]["blocks"][b] for p,b in POSITIONS]
        self.assertEqual(ids, [proof["id"] for proof in self.layout["editorial"][-len(POSITIONS):]])
        before = deepcopy(self.after)
        before["stateLayout"].pop("sourceFormatRevisions")
        before["stateLayout"]["editorial"] = before["stateLayout"]["editorial"][:-len(POSITIONS)]
        for p,b in POSITIONS:
            identity = self.layout["current"][p]["blocks"][b]
            old, new = self.archives[identity], self.after["pages"][p]["blocks"][b]
            self.assertTrue(require_source_format(old, new), identity)
            key = source_field(old)
            self.assertEqual(
                [line for line in old[key].splitlines() if line.strip()],
                [line for line in new[key].splitlines() if line.strip()], identity)
            self.assertIn("\n\n    while (", new[key], identity)
            self.assertIn("\n\n    return 0;", new[key], identity)
            before["pages"][p]["blocks"][b] = deepcopy(old)
        state.validate(before)
        state.validate_transition(before, self.after)
        self.assertEqual(state.validate(before), state.validate(self.after))
        for key in ("origin", "family", "current", "additions"):
            self.assertEqual(before["stateLayout"][key], self.layout[key])

    def test_cpp23_original_and_spaced_loops_have_identical_output(self):
        with tempfile.TemporaryDirectory(prefix="while-format-") as temp:
            for index,(p,b) in enumerate(POSITIONS):
                identity = self.layout["current"][p]["blocks"][b]
                original = self.archives[identity]
                updated = self.after["pages"][p]["blocks"][b]
                cases = updated.get("outputCheck", {}).get("cases", [])
                inputs = [case["standardInput"] for case in cases] or [
                    updated.get("initialStandardInput", "")]
                records = []
                for variant, block in (("original",original),("formatted",updated)):
                    path = Path(temp) / f"{index}-{variant}.cpp"
                    exe = Path(temp) / f"{index}-{variant}"
                    path.write_text(ready_source(block), encoding="utf-8")
                    built = subprocess.run(["c++","-std=c++23","-O0","-o",str(exe),str(path)],
                                           capture_output=True,text=True,timeout=90)
                    self.assertEqual(built.returncode, 0, identity + built.stderr)
                    executions=[]
                    for stdin in inputs:
                        result = subprocess.run([str(exe)], input=stdin,
                            capture_output=True,text=True,timeout=15)
                        self.assertEqual(result.returncode, 0, identity)
                        self.assertEqual(result.stderr, "", identity)
                        executions.append((result.returncode,result.stdout,result.stderr))
                    records.append(executions)
                self.assertEqual(records[0], records[1], identity)
                for result, case in zip(records[1], cases):
                    self.assertIn(result[1], case["outputs"], identity)
                if updated["type"]=="output_prediction":
                    self.assertIn(records[1][0][1],updated["answers"],identity)


if __name__ == "__main__":
    unittest.main()
