"""Regresión de recarga: separación física y estado histórico intactos."""
from copy import deepcopy
import json
from pathlib import Path
import subprocess
import tempfile
import unittest

import stable_state_layout as state
from code_format_compat import require_source_format

ROOT=Path(__file__).resolve().parents[2]
PATH=ROOT/"app/src/main/assets/content/es/programming/cpp_fundamentals/repeticion_y_terminacion/lesson_construir_un_proceso_de_recarga.json"
POSITIONS=((1,3),(2,1),(2,7),(3,1),(3,3),(3,8))
STARTERS={(1,3),(3,8)}


def slot(block):
    return "initialCode" if block["type"]=="cpp_compiler" else "code"


class RechargeFormatBatch(unittest.TestCase):
    def setUp(self):
        self.after=json.loads(PATH.read_text(encoding="utf-8"))
        self.meta=self.after["stateLayout"]
        self.history={x["id"]:x["block"] for x in self.meta["sourceFormatRevisions"]}

    def test_authenticated_transition_preserves_ids_answers_and_inputs(self):
        state.validate(self.after)
        self.assertEqual(len(POSITIONS),len(self.history))
        ids=[self.meta["current"][p]["blocks"][b] for p,b in POSITIONS]
        self.assertEqual(ids,[x["id"] for x in self.meta["editorial"][-len(POSITIONS):]])
        prior=deepcopy(self.after)
        prior["stateLayout"].pop("sourceFormatRevisions")
        prior["stateLayout"]["editorial"]=prior["stateLayout"]["editorial"][:-len(POSITIONS)]
        for p,b in POSITIONS:
            identity=self.meta["current"][p]["blocks"][b]
            before,new=self.history[identity],self.after["pages"][p]["blocks"][b]
            self.assertTrue(require_source_format(before,new),identity)
            key=slot(before)
            self.assertEqual([line for line in before[key].splitlines() if line.strip()],
                             [line for line in new[key].splitlines() if line.strip()],identity)
            self.assertEqual(before[key].count("} else {"),new[key].count("} else {"),identity)
            prior["pages"][p]["blocks"][b]=deepcopy(before)
        state.validate(prior)
        state.validate_transition(prior,self.after)
        self.assertEqual(state.validate(prior),state.validate(self.after))
        for field in ("origin","family","current","additions"):
            self.assertEqual(prior["stateLayout"][field],self.meta[field])

    def test_cpp23_recharge_cases_match_and_construction_remains_student_work(self):
        with tempfile.TemporaryDirectory(prefix="recharge-format-") as folder:
            for index,(p,b) in enumerate(POSITIONS):
                identity=self.meta["current"][p]["blocks"][b]
                before,new=self.history[identity],self.after["pages"][p]["blocks"][b]
                cases=new.get("outputCheck",{}).get("cases",[])
                inputs=[x["standardInput"] for x in cases] or [new.get("initialStandardInput","")]
                results=[]
                for label,block in (("old",before),("spaced",new)):
                    src=Path(folder)/f"{index}-{label}.cpp"
                    exe=Path(folder)/f"{index}-{label}"
                    src.write_text(block[slot(block)],encoding="utf-8")
                    compile=subprocess.run(["c++","-std=c++23","-O0","-o",str(exe),str(src)],
                                           capture_output=True,text=True,timeout=90)
                    self.assertEqual(0,compile.returncode,identity+compile.stderr)
                    runs=[]
                    for inp in inputs:
                        run=subprocess.run([str(exe)],input=inp,capture_output=True,text=True,timeout=15)
                        self.assertEqual(0,run.returncode,identity)
                        self.assertEqual("",run.stderr,identity)
                        runs.append((run.returncode,run.stdout,run.stderr))
                    results.append(runs)
                self.assertEqual(results[0],results[1],identity)
                if new["type"]=="output_prediction":
                    self.assertIn(results[1][0][1],new["answers"],identity)
                if (p,b) in STARTERS:
                    self.assertTrue(cases)
                    self.assertNotIn(results[1][0][1],cases[0]["outputs"],identity)
                else:
                    for output,case in zip(results[1],cases):
                        self.assertIn(output[1],case["outputs"],identity)


if __name__=="__main__":
    unittest.main()
