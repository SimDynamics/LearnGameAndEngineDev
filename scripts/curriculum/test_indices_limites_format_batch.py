"""U5L2: valid indices, empty vector, guarded at() and historical state."""
from copy import deepcopy
import json
from pathlib import Path
import subprocess
import tempfile
import unittest

import stable_state_layout as state
from code_format_compat import require_source_format

ROOT=Path(__file__).resolve().parents[2]
PATH=ROOT/"app/src/main/assets/content/es/programming/cpp_fundamentals/colecciones_y_recorridos/lesson_indices_y_limites_de_acceso.json"
POSITIONS=((0,2),(0,4),(2,2),(2,3),(3,2),(4,1),(4,2),(4,3),
           (5,2),(6,2),(7,1),(7,2),(7,7))
INCOMPLETE={(6,2)}


def field(block):
    return "initialCode" if block["type"]=="cpp_compiler" else "code"


def runnable(block):
    code=block[field(block)]
    if block["type"]=="fill_code":
        assert code.count("___")==1
        code=code.replace("___",block["answers"][0])
    return code


class IndicesLimitesFormat(unittest.TestCase):
    def setUp(self):
        self.doc=json.loads(PATH.read_text(encoding="utf-8"))
        self.meta=self.doc["stateLayout"]
        self.previous={item["id"]:item["block"] for item in self.meta["sourceFormatRevisions"]}

    def test_history_and_protected_conditions_preserved(self):
        state.validate(self.doc)
        self.assertEqual(len(POSITIONS),len(self.previous))
        ids=[self.meta["current"][p]["blocks"][b] for p,b in POSITIONS]
        self.assertEqual(ids,[item["id"] for item in self.meta["editorial"][-len(POSITIONS):]])
        original=deepcopy(self.doc)
        original["stateLayout"].pop("sourceFormatRevisions")
        original["stateLayout"]["editorial"]=original["stateLayout"]["editorial"][:-len(POSITIONS)]
        for p,b in POSITIONS:
            identity=self.meta["current"][p]["blocks"][b]
            before,after=self.previous[identity],self.doc["pages"][p]["blocks"][b]
            self.assertTrue(require_source_format(before,after),identity)
            key=field(before)
            self.assertEqual([line for line in before[key].splitlines() if line.strip()],
                             [line for line in after[key].splitlines() if line.strip()],identity)
            self.assertEqual(before[key].count("} else {"),after[key].count("} else {"),identity)
            self.assertEqual(before[key].count(".at("),after[key].count(".at("),identity)
            original["pages"][p]["blocks"][b]=deepcopy(before)
        state.validate(original)
        state.validate_transition(original,self.doc)
        self.assertEqual(state.validate(original),state.validate(self.doc))
        for key in ("origin","family","current","additions"):
            self.assertEqual(original["stateLayout"][key],self.meta[key])

    def test_all_optional_cpp23_inputs_and_outcomes_stay_identical(self):
        with tempfile.TemporaryDirectory(prefix="indices-format-") as folder:
            for index,(p,b) in enumerate(POSITIONS):
                identity=self.meta["current"][p]["blocks"][b]
                before,after=self.previous[identity],self.doc["pages"][p]["blocks"][b]
                cases=after.get("outputCheck",{}).get("cases",[])
                inputs=[c["standardInput"] for c in cases] or [after.get("initialStandardInput","")]
                observations=[]
                for label,block in (("old",before),("new",after)):
                    path=Path(folder)/f"{index}-{label}.cpp"
                    binary=Path(folder)/f"{index}-{label}"
                    path.write_text(runnable(block),encoding="utf-8")
                    built=subprocess.run(["c++","-std=c++23","-O0","-o",str(binary),str(path)],
                                         capture_output=True,text=True,timeout=90)
                    self.assertEqual(0,built.returncode,identity+built.stderr)
                    runs=[]
                    for stdin in inputs:
                        run=subprocess.run([str(binary)],input=stdin,capture_output=True,text=True,timeout=15)
                        self.assertEqual(0,run.returncode,identity)
                        self.assertEqual("",run.stderr,identity)
                        runs.append((run.returncode,run.stdout,run.stderr))
                    observations.append(runs)
                self.assertEqual(observations[0],observations[1],identity)
                if after["type"]=="output_prediction":
                    self.assertIn(observations[1][0][1],after["answers"],identity)
                if (p,b) in INCOMPLETE:
                    self.assertNotIn(observations[1][0][1],cases[0]["outputs"],identity)
                else:
                    for result,case in zip(observations[1],cases):
                        self.assertIn(result[1],case["outputs"],identity)


if __name__=="__main__":
    unittest.main()
