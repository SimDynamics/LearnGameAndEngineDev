"""Regresión U4L5: break y continue, sin completar el código del estudiante."""
from copy import deepcopy
import json
from pathlib import Path
import subprocess
import tempfile
import unittest

import stable_state_layout as state
from code_format_compat import require_source_format

ROOT = Path(__file__).resolve().parents[2]
PATH = ROOT / "app/src/main/assets/content/es/programming/cpp_fundamentals/repeticion_y_terminacion/lesson_interrumpir_o_continuar.json"
POSITIONS = ((0,2),(0,4),(1,1),(1,2),(2,2),(2,3),(2,4),
             (3,3),(4,1),(4,2),(4,4),(4,7),(5,2),(5,3))
UNSOLVED = {(4,4),(4,7)}


def source_field(block):
    return "initialCode" if block["type"]=="cpp_compiler" else "code"


def runnable(block):
    source=block[source_field(block)]
    if block["type"]=="fill_code":
        assert source.count("___")==1
        source=source.replace("___",block["answers"][0],1)
    return source


class BreakContinueFormatBatch(unittest.TestCase):
    def setUp(self):
        self.current=json.loads(PATH.read_text(encoding="utf-8"))
        self.state=self.current["stateLayout"]
        self.archive={record["id"]:record["block"] for record in self.state["sourceFormatRevisions"]}

    def test_transition_and_original_answer_history(self):
        state.validate(self.current)
        self.assertEqual(len(POSITIONS),len(self.archive))
        ids=[self.state["current"][p]["blocks"][b] for p,b in POSITIONS]
        self.assertEqual(ids,[e["id"] for e in self.state["editorial"][-len(POSITIONS):]])
        previous=deepcopy(self.current)
        previous["stateLayout"].pop("sourceFormatRevisions")
        previous["stateLayout"]["editorial"]=previous["stateLayout"]["editorial"][:-len(POSITIONS)]
        for p,b in POSITIONS:
            ident=self.state["current"][p]["blocks"][b]
            old,new=self.archive[ident],self.current["pages"][p]["blocks"][b]
            self.assertTrue(require_source_format(old,new),ident)
            field=source_field(old)
            self.assertEqual([l for l in old[field].splitlines() if l.strip()],
                             [l for l in new[field].splitlines() if l.strip()],ident)
            previous["pages"][p]["blocks"][b]=deepcopy(old)
        state.validate(previous)
        state.validate_transition(previous,self.current)
        self.assertEqual(state.validate(previous),state.validate(self.current))
        for field in ("origin","family","current","additions"):
            self.assertEqual(previous["stateLayout"][field],self.state[field])

    def test_cxx23_preserves_correct_answers_and_incomplete_editors(self):
        with tempfile.TemporaryDirectory(prefix="break-continue-format-") as folder:
            for i,(p,b) in enumerate(POSITIONS):
                ident=self.state["current"][p]["blocks"][b]
                old,new=self.archive[ident],self.current["pages"][p]["blocks"][b]
                cases=new.get("outputCheck",{}).get("cases",[])
                inputs=[case["standardInput"] for case in cases] or [new.get("initialStandardInput","")]
                observations=[]
                for label,block in (("old",old),("spaced",new)):
                    src=Path(folder)/f"{i}-{label}.cpp"
                    exe=Path(folder)/f"{i}-{label}"
                    src.write_text(runnable(block),encoding="utf-8")
                    built=subprocess.run(["c++","-std=c++23","-O0","-o",str(exe),str(src)],
                                         capture_output=True,text=True,timeout=90)
                    self.assertEqual(0,built.returncode,ident+built.stderr)
                    results=[]
                    for data in inputs:
                        r=subprocess.run([str(exe)],input=data,capture_output=True,text=True,timeout=15)
                        self.assertEqual(0,r.returncode,ident)
                        self.assertEqual("",r.stderr,ident)
                        results.append((r.returncode,r.stdout,r.stderr))
                    observations.append(results)
                self.assertEqual(observations[0],observations[1],ident)
                if new["type"]=="output_prediction":
                    self.assertIn(observations[1][0][1],new["answers"],ident)
                if (p,b) in UNSOLVED:
                    self.assertNotIn(observations[1][0][1],cases[0]["outputs"],ident)
                else:
                    for result,case in zip(observations[1],cases):
                        self.assertIn(result[1],case["outputs"],ident)


if __name__=="__main__":
    unittest.main()
