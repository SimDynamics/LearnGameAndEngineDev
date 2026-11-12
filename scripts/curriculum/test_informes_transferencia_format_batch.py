"""U5L12: numerical reports, copy versus reference and historical contracts."""
from copy import deepcopy
import json
from pathlib import Path
import subprocess
import tempfile
import unittest

import stable_state_layout as state
from code_format_compat import require_source_format

ROOT=Path(__file__).resolve().parents[2]
LESSON=ROOT/"app/src/main/assets/content/es/programming/cpp_fundamentals/colecciones_y_recorridos/lesson_informes_y_transferencia_de_colecciones.json"
POSITIONS=((2,2),(2,14),(3,1),(3,6),(4,1),(4,2),(6,2),(6,15),(7,1),(7,5),(7,9),
           (8,1),(9,7),(9,19),(9,22),(9,25),(9,28))
UNSOLVED={(2,2),(2,14),(6,2),(6,15),(9,7),(9,19),(9,25)}

def field(block):
    return "initialCode" if block["type"]=="cpp_compiler" else "code"

def runnable(block):
    src=block[field(block)]
    if block["type"]=="fill_code":
        assert src.count("___")==1
        src=src.replace("___",block["answers"][0],1)
    return src

class ReportsTransferFormat(unittest.TestCase):
    def setUp(self):
        self.doc=json.loads(LESSON.read_text(encoding="utf-8"))
        self.state=self.doc["stateLayout"]
        self.old={v["id"]:v["block"] for v in self.state["sourceFormatRevisions"]}

    def test_original_identity_responses_and_editor_state_preserved(self):
        state.validate(self.doc)
        self.assertEqual(len(POSITIONS),len(self.old))
        self.assertEqual([self.state["current"][p]["blocks"][b] for p,b in POSITIONS],
                         [x["id"] for x in self.state["editorial"][-len(POSITIONS):]])
        previous=deepcopy(self.doc)
        previous["stateLayout"].pop("sourceFormatRevisions")
        previous["stateLayout"]["editorial"]=previous["stateLayout"]["editorial"][:-len(POSITIONS)]
        for p,b in POSITIONS:
            identity=self.state["current"][p]["blocks"][b]
            before,after=self.old[identity],self.doc["pages"][p]["blocks"][b]
            self.assertTrue(require_source_format(before,after),identity)
            key=field(before)
            self.assertEqual([line for line in before[key].splitlines() if line.strip()],
                             [line for line in after[key].splitlines() if line.strip()],identity)
            previous["pages"][p]["blocks"][b]=deepcopy(before)
        state.validate(previous)
        state.validate_transition(previous,self.doc)
        self.assertEqual(state.validate(previous),state.validate(self.doc))
        for k in ("origin","family","current","additions"):
            self.assertEqual(previous["stateLayout"][k],self.state[k])

    def test_cpp23_original_vs_formatted_all_declared_cases(self):
        with tempfile.TemporaryDirectory(prefix="reports-format-") as directory:
            for i,(p,b) in enumerate(POSITIONS):
                ident=self.state["current"][p]["blocks"][b]
                old,new=self.old[ident],self.doc["pages"][p]["blocks"][b]
                cases=new.get("outputCheck",{}).get("cases",[])
                inputs=[case["standardInput"] for case in cases] or [new.get("initialStandardInput","")]
                records=[]
                for tag,block in (("old",old),("new",new)):
                    source=Path(directory)/f"{i}-{tag}.cpp"
                    executable=Path(directory)/f"{i}-{tag}"
                    source.write_text(runnable(block),encoding="utf-8")
                    build=subprocess.run(["c++","-std=c++23","-O0","-o",str(executable),str(source)],
                                         capture_output=True,text=True,timeout=90)
                    self.assertEqual(0,build.returncode,ident+build.stderr)
                    runs=[]
                    for stdin in inputs:
                        r=subprocess.run([str(executable)],input=stdin,capture_output=True,text=True,timeout=15)
                        self.assertEqual(0,r.returncode,ident)
                        self.assertEqual("",r.stderr,ident)
                        runs.append((r.returncode,r.stdout,r.stderr))
                    records.append(runs)
                self.assertEqual(records[0],records[1],ident)
                if new["type"]=="output_prediction":
                    self.assertIn(records[1][0][1],new["answers"],ident)
                if (p,b) in UNSOLVED:
                    self.assertTrue(cases,ident)
                    self.assertNotIn(records[1][0][1],cases[0]["outputs"],ident)
                else:
                    for result,case in zip(records[1],cases):
                        self.assertIn(result[1],case["outputs"],ident)

if __name__=="__main__":
    unittest.main()
