"""U6L4: historical state and C++23 source preservation through physical spacing."""
from copy import deepcopy
import json
from pathlib import Path
import subprocess
import tempfile
import unittest

import stable_state_layout as state
from code_format_compat import require_source_format

ROOT = Path(__file__).resolve().parents[2]
PATH = ROOT / "app/src/main/assets/content/es/programming/cpp_fundamentals/funciones_y_contratos/lesson_copia_y_ambito.json"
POSITIONS = ((0,2),(0,4),(1,2),(2,2),(3,2),(3,7),(3,10),(5,3))
UNSOLVED_STARTER = (3,10)

def source_slot(block):
    return "initialCode" if block["type"] == "cpp_compiler" else "code"

class CopyAndScopeFormatBatch(unittest.TestCase):
    def setUp(self):
        self.current = json.loads(PATH.read_text(encoding="utf-8"))
        self.meta = self.current["stateLayout"]
        self.originals = {entry["id"]:entry["block"]
                          for entry in self.meta["sourceFormatRevisions"]}

    def test_historical_storage_and_unchanged_single_line_cards(self):
        state.validate(self.current)
        self.assertEqual(len(POSITIONS),len(self.originals))
        ids = [self.meta["current"][p]["blocks"][b] for p,b in POSITIONS]
        self.assertEqual(ids,[entry["id"] for entry in self.meta["editorial"][-len(POSITIONS):]])
        previous=deepcopy(self.current)
        previous["stateLayout"].pop("sourceFormatRevisions")
        previous["stateLayout"]["editorial"]=previous["stateLayout"]["editorial"][:-len(POSITIONS)]
        for p,b in POSITIONS:
            identity=self.meta["current"][p]["blocks"][b]
            old,new=self.originals[identity],self.current["pages"][p]["blocks"][b]
            self.assertTrue(require_source_format(old,new),identity)
            self.assertEqual([line for line in old[source_slot(old)].splitlines() if line.strip()],
                             [line for line in new[source_slot(new)].splitlines() if line.strip()],identity)
            self.assertGreater(new[source_slot(new)].count("\n\n"),
                               old[source_slot(old)].count("\n\n"),identity)
            self.assertEqual(old.get("outputCheck"),new.get("outputCheck"),identity)
            self.assertEqual(old.get("answers"),new.get("answers"),identity)
            previous["pages"][p]["blocks"][b]=deepcopy(old)
        state.validate(previous)
        state.validate_transition(previous,self.current)
        self.assertEqual(state.validate(previous),state.validate(self.current))
        for key in ("origin","current","family","additions","orderRefinements"):
            self.assertEqual(previous["stateLayout"][key],self.meta[key])
        self.assertEqual(previous["orderVisualSeparators"],self.current["orderVisualSeparators"])
        cards=self.current["pages"][4]["blocks"][1]
        self.assertEqual("order_code",cards["type"])
        self.assertEqual(10,len(cards["fragments"]))
        self.assertEqual(2,self.current["orderVisualSeparators"][0]["count"])
        self.assertTrue(all(fragment.strip() and "\n" not in fragment and "\r" not in fragment
                            for fragment in cards["fragments"]))

    def test_cpp23_original_and_formatted_outputs(self):
        with tempfile.TemporaryDirectory(prefix="u6l4-copy-scope-") as folder:
            for index,(p,b) in enumerate(POSITIONS):
                identity=self.meta["current"][p]["blocks"][b]
                old,new=self.originals[identity],self.current["pages"][p]["blocks"][b]
                cases=new.get("outputCheck",{}).get("cases",[])
                inputs=[case["standardInput"] for case in cases] or [""]
                variants=[]
                for version,block in (("historical",old),("formatted",new)):
                    source=Path(folder)/f"{index}-{version}.cpp"
                    executable=Path(folder)/f"{index}-{version}"
                    source.write_text(block[source_slot(block)],encoding="utf-8")
                    compiled=subprocess.run(["c++","-std=c++23","-O0","-o",str(executable),str(source)],
                                            capture_output=True,text=True,timeout=90)
                    self.assertEqual(0,compiled.returncode,identity+compiled.stderr)
                    outputs=[]
                    for data in inputs:
                        run=subprocess.run([str(executable)],input=data,capture_output=True,
                                           text=True,timeout=15)
                        self.assertEqual(0,run.returncode,identity+run.stderr)
                        self.assertEqual("",run.stderr,identity)
                        outputs.append((run.returncode,run.stdout,run.stderr))
                    variants.append(outputs)
                self.assertEqual(variants[0],variants[1],identity)
                if new["type"]=="output_prediction":
                    self.assertIn(variants[1][0][1].rstrip("\n"),new["answers"],identity)
                if cases:
                    for result,case in zip(variants[1],cases):
                        if (p,b)==UNSOLVED_STARTER:
                            self.assertNotIn(result[1],case["outputs"],identity)
                        else:
                            self.assertIn(result[1],case["outputs"],identity)

if __name__=="__main__":
    unittest.main()
