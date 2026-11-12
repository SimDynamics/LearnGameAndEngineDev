"""U6L1 named functions: preserve historic state and C++23 behavior while spacing source."""
from copy import deepcopy
import json
from pathlib import Path
import subprocess
import tempfile
import unittest

import stable_state_layout as state
from code_format_compat import require_source_format

ROOT = Path(__file__).resolve().parents[2]
PATH = ROOT / "app/src/main/assets/content/es/programming/cpp_fundamentals/funciones_y_contratos/lesson_nombrar_calculos_y_llamar.json"
POSITIONS = ((0,2), (1,2), (1,3), (2,2), (3,2), (3,7), (3,10),
             (4,1), (5,3), (5,7), (5,11))
UNSOLVED = {(5,7)}
NOT_COMPILABLE = {(1,3)}

def source_slot(block):
    return "initialCode" if block["type"] == "cpp_compiler" else "code"

class NamedFunctionsFormatBatch(unittest.TestCase):
    def setUp(self):
        self.current = json.loads(PATH.read_text(encoding="utf-8"))
        self.meta = self.current["stateLayout"]
        self.originals = {entry["id"]: entry["block"]
                          for entry in self.meta["sourceFormatRevisions"]}

    def test_historical_identity_and_all_source_lines(self):
        state.validate(self.current)
        self.assertEqual(len(POSITIONS), len(self.originals))
        self.assertEqual(2, self.current["orderVisualSeparators"][0]["count"])
        ids = [self.meta["current"][p]["blocks"][b] for p,b in POSITIONS]
        self.assertEqual(ids, [edge["id"] for edge in
                               self.meta["editorial"][-len(POSITIONS):]])
        previous = deepcopy(self.current)
        previous["stateLayout"].pop("sourceFormatRevisions")
        previous["stateLayout"]["editorial"] = previous["stateLayout"]["editorial"][:-len(POSITIONS)]
        for p,b in POSITIONS:
            ident = self.meta["current"][p]["blocks"][b]
            old, new = self.originals[ident], self.current["pages"][p]["blocks"][b]
            self.assertTrue(require_source_format(old, new), ident)
            slot = source_slot(old)
            self.assertEqual([line for line in old[slot].splitlines() if line.strip()],
                             [line for line in new[slot].splitlines() if line.strip()], ident)
            self.assertGreater(new[slot].count("\n\n"), old[slot].count("\n\n"), ident)
            previous["pages"][p]["blocks"][b] = deepcopy(old)
        state.validate(previous)
        state.validate_transition(previous, self.current)
        self.assertEqual(state.validate(previous), state.validate(self.current))
        for field in ("origin", "current", "family", "additions", "orderRefinements"):
            self.assertEqual(previous["stateLayout"][field], self.meta[field])
        self.assertEqual(previous["orderVisualSeparators"],
                         self.current["orderVisualSeparators"])

    def test_cpp23_before_and_after_have_identical_behavior(self):
        with tempfile.TemporaryDirectory(prefix="functions-u6l1-format-") as folder:
            for i,(p,b) in enumerate(POSITIONS):
                if (p,b) in NOT_COMPILABLE:
                    continue  # Fill-code deliberately contains a learner blank.
                ident = self.meta["current"][p]["blocks"][b]
                old, new = self.originals[ident], self.current["pages"][p]["blocks"][b]
                cases = new.get("outputCheck", {}).get("cases", [])
                inputs = [case["standardInput"] for case in cases] or [""]
                runs = []
                for label,block in (("historical",old), ("formatted",new)):
                    source = Path(folder) / f"{i}-{label}.cpp"
                    executable = Path(folder) / f"{i}-{label}"
                    source.write_text(block[source_slot(block)],encoding="utf-8")
                    built = subprocess.run(["c++","-std=c++23","-O0","-o",str(executable),str(source)],
                                           capture_output=True,text=True,timeout=90)
                    self.assertEqual(0,built.returncode,ident+built.stderr)
                    output = []
                    for data in inputs:
                        result = subprocess.run([str(executable)],input=data,
                                                capture_output=True,text=True,timeout=15)
                        self.assertEqual(0,result.returncode,ident+result.stderr)
                        output.append((result.returncode,result.stdout,result.stderr))
                    runs.append(output)
                self.assertEqual(runs[0],runs[1],ident)
                if new["type"] == "output_prediction":
                    self.assertIn(runs[1][0][1].rstrip("\n"),new["answers"],ident)
                elif cases:
                    for result,case in zip(runs[1],cases):
                        if (p,b) in UNSOLVED:
                            self.assertNotIn(result[1],case["outputs"],ident)
                        else:
                            self.assertIn(result[1],case["outputs"],ident)

if __name__ == "__main__":
    unittest.main()
