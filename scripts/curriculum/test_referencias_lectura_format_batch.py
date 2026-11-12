"""U6L5: preserve const-reference code, responses, and historical contracts in C++23."""
from copy import deepcopy
import json
from pathlib import Path
import subprocess
import tempfile
import unittest

import stable_state_layout as state
from code_format_compat import require_source_format

ROOT = Path(__file__).resolve().parents[2]
PATH = ROOT / "app/src/main/assets/content/es/programming/cpp_fundamentals/funciones_y_contratos/lesson_referencias_para_leer.json"
POSITIONS = ((0,2),(0,4),(1,2),(1,4),(2,2),(3,3),(4,2),(5,3),(5,7))

def source_slot(block):
    if block["type"] == "cpp_compiler":
        return "initialCode"
    if block["type"] == "inline_fill_code":
        return "template"
    return "code"

def runnable(block):
    source = block[source_slot(block)]
    if block["type"] == "inline_fill_code":
        self_answers = block["answers"]
        assert source.count("[]") == len(self_answers) == 2
        for answer in self_answers:
            source = source.replace("[]", answer, 1)
    return source

class ConstReferenceFormatBatch(unittest.TestCase):
    def setUp(self):
        self.document = json.loads(PATH.read_text(encoding="utf-8"))
        self.meta = self.document["stateLayout"]
        self.originals = {entry["id"]: entry["block"]
                          for entry in self.meta["sourceFormatRevisions"]}

    def test_state_and_source_preservation(self):
        state.validate(self.document)
        self.assertEqual(9, len(POSITIONS))
        self.assertEqual(len(POSITIONS), len(self.originals))
        self.assertEqual(
            [self.meta["current"][p]["blocks"][b] for p,b in POSITIONS],
            [e["id"] for e in self.meta["editorial"][-len(POSITIONS):]],
        )
        previous = deepcopy(self.document)
        previous["stateLayout"].pop("sourceFormatRevisions")
        previous["stateLayout"]["editorial"] = previous["stateLayout"]["editorial"][:-len(POSITIONS)]
        for p,b in POSITIONS:
            identity = self.meta["current"][p]["blocks"][b]
            old = self.originals[identity]
            new = self.document["pages"][p]["blocks"][b]
            self.assertTrue(require_source_format(old,new),identity)
            slot = source_slot(old)
            self.assertEqual(
                [line for line in old[slot].splitlines() if line.strip()],
                [line for line in new[slot].splitlines() if line.strip()],
                identity,
            )
            self.assertGreater(new[slot].count("\n\n"),old[slot].count("\n\n"),identity)
            self.assertEqual(old.get("outputCheck"),new.get("outputCheck"),identity)
            self.assertEqual(old.get("answers"),new.get("answers"),identity)
            previous["pages"][p]["blocks"][b] = deepcopy(old)
        state.validate(previous)
        state.validate_transition(previous,self.document)
        self.assertEqual(state.validate(previous),state.validate(self.document))
        for key in ("origin","current","family","additions"):
            self.assertEqual(previous["stateLayout"][key],self.meta[key])

    def test_cpp23_behavior_and_optional_inputs(self):
        with tempfile.TemporaryDirectory(prefix="u6l5-const-reference-") as folder:
            for index,(p,b) in enumerate(POSITIONS):
                identity = self.meta["current"][p]["blocks"][b]
                old,new = self.originals[identity], self.document["pages"][p]["blocks"][b]
                cases = new.get("outputCheck",{}).get("cases",[])
                inputs = [case["standardInput"] for case in cases] or [""]
                variants = []
                for label,block in (("historical",old),("formatted",new)):
                    source = Path(folder)/f"{index}-{label}.cpp"
                    executable = Path(folder)/f"{index}-{label}"
                    source.write_text(runnable(block),encoding="utf-8")
                    built = subprocess.run(["c++","-std=c++23","-O0","-o",str(executable),str(source)],
                                           capture_output=True,text=True,timeout=90)
                    self.assertEqual(0,built.returncode,identity+built.stderr)
                    results = []
                    for data in inputs:
                        run = subprocess.run([str(executable)],input=data,capture_output=True,
                                             text=True,timeout=15)
                        self.assertEqual(0,run.returncode,identity+run.stderr)
                        self.assertEqual("",run.stderr,identity)
                        results.append((run.returncode,run.stdout,run.stderr))
                    variants.append(results)
                self.assertEqual(variants[0],variants[1],identity)
                if new["type"] == "output_prediction":
                    self.assertIn(variants[1][0][1].rstrip("\n"),new["answers"],identity)
                if new["type"] == "inline_fill_code":
                    self.assertEqual(["const","&"],new["answers"],identity)
                    self.assertEqual("1",variants[1][0][1],identity)
                if cases:
                    for result,case in zip(variants[1],cases):
                        self.assertIn(result[1],case["outputs"],identity)
        self.assertEqual(3,len(self.document["pages"][5]["blocks"][7]["outputCheck"]["cases"]))

if __name__ == "__main__":
    unittest.main()
