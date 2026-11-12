"""U5L13: frequency ties and authentic old and new C++23 programs."""
from copy import deepcopy
import json
from pathlib import Path
import subprocess
import tempfile
import unittest

import stable_state_layout as state
from code_format_compat import require_source_format

ROOT=Path(__file__).resolve().parents[2]
PATH=ROOT/"app/src/main/assets/content/es/programming/cpp_fundamentals/colecciones_y_recorridos/lesson_contar_frecuencias_y_conservar_empates.json"
POSITIONS=((1,2),(2,5),(3,2),(3,9),(4,2),(4,4),(5,5),(5,16),
           (6,2),(6,10),(6,13),(7,2),(7,4),(7,10),(7,19),(7,22))
UNSOLVED={(2,5),(5,5),(5,16),(7,10),(7,19)}

def slot(block):
    return "initialCode" if block["type"]=="cpp_compiler" else "code"

class FrequencyFormatBatch(unittest.TestCase):
    def setUp(self):
        self.current=json.loads(PATH.read_text(encoding="utf-8"))
        self.meta=self.current["stateLayout"]
        self.originals={entry["id"]:entry["block"] for entry in self.meta["sourceFormatRevisions"]}

    def test_historical_state_and_source_are_unchanged(self):
        state.validate(self.current)
        self.assertEqual(len(POSITIONS),len(self.originals))
        ids=[self.meta["current"][p]["blocks"][b] for p,b in POSITIONS]
        self.assertEqual(ids,[edge["id"] for edge in self.meta["editorial"][-len(POSITIONS):]])
        previous=deepcopy(self.current)
        previous["stateLayout"].pop("sourceFormatRevisions")
        previous["stateLayout"]["editorial"]=previous["stateLayout"]["editorial"][:-len(POSITIONS)]
        for p,b in POSITIONS:
            ident=self.meta["current"][p]["blocks"][b]
            old,new=self.originals[ident],self.current["pages"][p]["blocks"][b]
            self.assertTrue(require_source_format(old,new),ident)
            key=slot(old)
            self.assertEqual([line for line in old[key].splitlines() if line.strip()],
                             [line for line in new[key].splitlines() if line.strip()],ident)
            self.assertEqual(old[key].count("} else if ("),new[key].count("} else if ("),ident)
            previous["pages"][p]["blocks"][b]=deepcopy(old)
        state.validate(previous)
        state.validate_transition(previous,self.current)
        self.assertEqual(state.validate(previous),state.validate(self.current))
        for key in ("origin","family","current","additions"):
            self.assertEqual(previous["stateLayout"][key],self.meta[key])

    def test_cpp23_same_outputs_and_optional_oracles(self):
        with tempfile.TemporaryDirectory(prefix="frequency-ties-format-") as folder:
            for i,(p,b) in enumerate(POSITIONS):
                ident=self.meta["current"][p]["blocks"][b]
                old,new=self.originals[ident],self.current["pages"][p]["blocks"][b]
                cases=new.get("outputCheck",{}).get("cases",[])
                inputs=[case["standardInput"] for case in cases] or [new.get("initialStandardInput","")]
                variants=[]
                for tag,block in (("before",old),("after",new)):
                    source=Path(folder)/f"{i}-{tag}.cpp"
                    exe=Path(folder)/f"{i}-{tag}"
                    source.write_text(block[slot(block)],encoding="utf-8")
                    built=subprocess.run(["c++","-std=c++23","-O0","-o",str(exe),str(source)],
                                         capture_output=True,text=True,timeout=90)
                    self.assertEqual(0,built.returncode,ident+built.stderr)
                    outputs=[]
                    for data in inputs:
                        run=subprocess.run([str(exe)],input=data,capture_output=True,text=True,timeout=15)
                        self.assertEqual(0,run.returncode,ident)
                        self.assertEqual("",run.stderr,ident)
                        outputs.append((run.returncode,run.stdout,run.stderr))
                    variants.append(outputs)
                self.assertEqual(variants[0],variants[1],ident)
                if new["type"]=="output_prediction":
                    self.assertIn(variants[1][0][1].rstrip("\n"),new["answers"],ident)
                if (p,b) in UNSOLVED:
                    self.assertTrue(cases,ident)
                    self.assertNotIn(variants[1][0][1],cases[0]["outputs"],ident)
                else:
                    for result,case in zip(variants[1],cases):
                        self.assertIn(result[1],case["outputs"],ident)

if __name__=="__main__":
    unittest.main()
