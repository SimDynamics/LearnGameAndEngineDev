"""U6L3: source spacing, original C++23 outputs, historical identity and all declared cases."""
from copy import deepcopy
import json
from pathlib import Path
import subprocess
import tempfile
import unittest

import stable_state_layout as state
from code_format_compat import require_source_format

ROOT = Path(__file__).resolve().parents[2]
PATH = ROOT / "app/src/main/assets/content/es/programming/cpp_fundamentals/funciones_y_contratos/lesson_retorno_y_caminos.json"
POSITIONS = ((0,2),(0,4),(1,2),(2,2),(3,2),(3,3),(4,3),(5,3),(5,7))
SLOTS = {"cpp_compiler": "initialCode", "drag_fill_code": "template"}

def slot(block):
    return SLOTS.get(block["type"],"code")

def completed(block):
    source=block[slot(block)]
    if block["type"]=="fill_code":
        self_answer=block["answers"][0]
        return source.replace("___",self_answer,1)
    if block["type"]=="drag_fill_code":
        for choice in block["correctOrder"]:
            source=source.replace("[]",block["choices"][choice],1)
    return source

class FunctionReturnFormatBatch(unittest.TestCase):
    def setUp(self):
        self.document=json.loads(PATH.read_text(encoding="utf-8"))
        self.meta=self.document["stateLayout"]
        self.original={e["id"]:e["block"] for e in self.meta["sourceFormatRevisions"]}

    def test_state_identity_and_source_only(self):
        state.validate(self.document)
        self.assertEqual(len(POSITIONS),len(self.original))
        self.assertEqual(
            [self.meta["current"][p]["blocks"][b] for p,b in POSITIONS],
            [e["id"] for e in self.meta["editorial"][-len(POSITIONS):]])
        previous=deepcopy(self.document)
        previous["stateLayout"].pop("sourceFormatRevisions")
        previous["stateLayout"]["editorial"]=previous["stateLayout"]["editorial"][:-len(POSITIONS)]
        for p,b in POSITIONS:
            identity=self.meta["current"][p]["blocks"][b]
            old=self.original[identity]
            new=self.document["pages"][p]["blocks"][b]
            self.assertTrue(require_source_format(old,new),identity)
            old_lines=[x for x in old[slot(old)].splitlines() if x.strip()]
            new_lines=[x for x in new[slot(new)].splitlines() if x.strip()]
            self.assertEqual(old_lines,new_lines,identity)
            self.assertGreater(new[slot(new)].count("\n\n"),old[slot(old)].count("\n\n"),identity)
            self.assertEqual(old.get("outputCheck"),new.get("outputCheck"),identity)
            self.assertEqual(old.get("answers"),new.get("answers"),identity)
            self.assertEqual(old.get("choices"),new.get("choices"),identity)
            self.assertEqual(old.get("correctOrder"),new.get("correctOrder"),identity)
            previous["pages"][p]["blocks"][b]=deepcopy(old)
        state.validate(previous)
        state.validate_transition(previous,self.document)
        self.assertEqual(state.validate(previous),state.validate(self.document))
        for key in ("origin","current","family","additions"):
            self.assertEqual(previous["stateLayout"][key],self.meta[key])

    def test_compilation_originals_and_optional_cases_cpp23(self):
        with tempfile.TemporaryDirectory(prefix="u6l3-return-") as root:
            for i,(p,b) in enumerate(POSITIONS):
                identity=self.meta["current"][p]["blocks"][b]
                old=self.original[identity]
                new=self.document["pages"][p]["blocks"][b]
                cases=new.get("outputCheck",{}).get("cases",[])
                inputs=[case["standardInput"] for case in cases] or [""]
                variants=[]
                for label,block in (("old",old),("new",new)):
                    source=Path(root)/f"{i}-{label}.cpp"
                    executable=Path(root)/f"{i}-{label}"
                    source.write_text(completed(block),encoding="utf-8")
                    built=subprocess.run(["c++","-std=c++23","-O0",str(source),"-o",str(executable)],
                                         capture_output=True,text=True,timeout=90)
                    self.assertEqual(0,built.returncode,identity+built.stderr)
                    results=[]
                    for data in inputs:
                        run=subprocess.run([str(executable)],input=data,capture_output=True,
                                           text=True,timeout=15)
                        self.assertEqual(0,run.returncode,identity+run.stderr)
                        self.assertEqual("",run.stderr,identity)
                        results.append(run.stdout)
                    variants.append(results)
                self.assertEqual(variants[0],variants[1],identity)
                if new["type"]=="output_prediction":
                    self.assertIn(variants[1][0].rstrip("\n"),new["answers"],identity)
                if cases:
                    for actual,case in zip(variants[1],cases):
                        self.assertIn(actual,case["outputs"],identity)
                if new["type"]=="fill_code":
                    self.assertEqual("0",variants[1][0],identity)
                if new["type"]=="drag_fill_code":
                    self.assertEqual("1",variants[1][0],identity)

if __name__=="__main__":
    unittest.main()
