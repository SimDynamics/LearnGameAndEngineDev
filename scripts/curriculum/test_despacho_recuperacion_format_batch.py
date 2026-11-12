"""Regresion U4L9: despacho, cajas, contadores y preservacion historica."""
from copy import deepcopy
import json
from pathlib import Path
import subprocess
import tempfile
import unittest

import stable_state_layout as state
from code_format_compat import require_source_format

ROOT=Path(__file__).resolve().parents[2]
PATH=ROOT/"app/src/main/assets/content/es/programming/cpp_fundamentals/repeticion_y_terminacion/lesson_despacho_y_recuperacion_de_recorridos.json"
POSITIONS=((1,2),(2,1),(2,2),(2,3),(3,1),(3,2),
           (3,3),(4,2),(4,3),(5,2),(5,7))
UNSOLVED=(1,2)


def source_field(block):
    return "initialCode" if block["type"]=="cpp_compiler" else "code"


def runnable(block):
    value=block[source_field(block)]
    if block["type"]=="fill_code":
        assert value.count("___")==1
        value=value.replace("___",block["answers"][0],1)
    return value


class DespachoHistoryFormat(unittest.TestCase):
    def setUp(self):
        self.current=json.loads(PATH.read_text(encoding="utf-8"))
        self.meta=self.current["stateLayout"]
        self.archives={entry["id"]:entry["block"] for entry in self.meta["sourceFormatRevisions"]}

    def test_preserves_identity_answers_and_original_source(self):
        state.validate(self.current)
        self.assertEqual(len(POSITIONS),len(self.archives))
        ids=[self.meta["current"][p]["blocks"][b] for p,b in POSITIONS]
        self.assertEqual(ids,[x["id"] for x in self.meta["editorial"][-len(POSITIONS):]])
        old=deepcopy(self.current)
        old["stateLayout"].pop("sourceFormatRevisions")
        old["stateLayout"]["editorial"]=old["stateLayout"]["editorial"][:-len(POSITIONS)]
        for p,b in POSITIONS:
            ident=self.meta["current"][p]["blocks"][b]
            before,after=self.archives[ident],self.current["pages"][p]["blocks"][b]
            self.assertTrue(require_source_format(before,after),ident)
            key=source_field(before)
            self.assertEqual([line for line in before[key].splitlines() if line.strip()],
                             [line for line in after[key].splitlines() if line.strip()],ident)
            old["pages"][p]["blocks"][b]=deepcopy(before)
        state.validate(old)
        state.validate_transition(old,self.current)
        self.assertEqual(state.validate(old),state.validate(self.current))
        for field in ("origin","family","current","additions"):
            self.assertEqual(old["stateLayout"][field],self.meta[field])

    def test_cpp23_outputs_match_historical_results_and_starter_remains_unsolved(self):
        with tempfile.TemporaryDirectory(prefix="despacho-format-") as folder:
            for index,(p,b) in enumerate(POSITIONS):
                ident=self.meta["current"][p]["blocks"][b]
                before,after=self.archives[ident],self.current["pages"][p]["blocks"][b]
                cases=after.get("outputCheck",{}).get("cases",[])
                inputs=[x["standardInput"] for x in cases] or [after.get("initialStandardInput","")]
                observations=[]
                for label,block in (("before",before),("after",after)):
                    source=Path(folder)/f"{index}-{label}.cpp"
                    exe=Path(folder)/f"{index}-{label}"
                    source.write_text(runnable(block),encoding="utf-8")
                    built=subprocess.run(["c++","-std=c++23","-O0","-o",str(exe),str(source)],
                                         capture_output=True,text=True,timeout=90)
                    self.assertEqual(0,built.returncode,ident+built.stderr)
                    runs=[]
                    for stdin in inputs:
                        run=subprocess.run([str(exe)],input=stdin,capture_output=True,text=True,timeout=15)
                        self.assertEqual(0,run.returncode,ident)
                        self.assertEqual("",run.stderr,ident)
                        runs.append((run.returncode,run.stdout,run.stderr))
                    observations.append(runs)
                self.assertEqual(observations[0],observations[1],ident)
                if after["type"]=="output_prediction":
                    self.assertIn(observations[1][0][1],after["answers"],ident)
                if (p,b)==UNSOLVED:
                    self.assertTrue(cases)
                    self.assertNotIn(observations[1][0][1],cases[0]["outputs"],ident)
                else:
                    for result,case in zip(observations[1],cases):
                        self.assertIn(result[1],case["outputs"],ident)


if __name__=="__main__":
    unittest.main()
