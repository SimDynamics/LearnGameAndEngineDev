"""U6L6: validate historic source-format transition and authentic C++23 behavior.

Regression includes intentionally unsolved editors, by-value range loops, aliasing,
all declared optional inputs, and unchanged answers/progression contracts.
"""
from copy import deepcopy
import json
from pathlib import Path
import subprocess
import tempfile
import unittest

import stable_state_layout as state
from code_format_compat import require_source_format

ROOT = Path(__file__).resolve().parents[2]
PATH = ROOT / "app/src/main/assets/content/es/programming/cpp_fundamentals/funciones_y_contratos/lesson_referencias_para_modificar.json"
POSITIONS = ((0,2),(0,4),(1,3),(2,2),(2,4),(3,2),(4,2),(5,3),(5,7),(5,10),(6,3),(7,5),(8,2),(8,8),(9,2),(10,2),(11,2),(11,5),(12,4),(13,2),(13,7),(14,7),(15,2))
UNSOLVED = {(7,5),(12,4),(14,7)}
FILL_BLANKS = {(1,3)}

def source_slot(block):
    return "initialCode" if block["type"] == "cpp_compiler" else "code"

def runnable_code(block):
    code = block[source_slot(block)]
    if block["type"] == "fill_code":
        self_answers = block["answers"]
        assert len(self_answers) == 1 and code.count("___") == 1
        return code.replace("___",self_answers[0],1)
    return code

class MutatingReferenceSourceFormatBatch(unittest.TestCase):
    def setUp(self):
        self.document=json.loads(PATH.read_text(encoding="utf-8"))
        self.meta=self.document["stateLayout"]
        self.originals={e["id"]: e["block"]
                        for e in self.meta["sourceFormatRevisions"]}

    def test_source_format_only_and_historical_state(self):
        state.validate(self.document)
        self.assertEqual(23,len(POSITIONS))
        self.assertEqual(len(POSITIONS),len(self.originals))
        ids=[self.meta["current"][p]["blocks"][b] for p,b in POSITIONS]
        self.assertEqual(ids,[e["id"] for e in self.meta["editorial"][-len(POSITIONS):]])
        previous=deepcopy(self.document)
        previous["stateLayout"].pop("sourceFormatRevisions")
        previous["stateLayout"]["editorial"]=previous["stateLayout"]["editorial"][:-len(POSITIONS)]
        for p,b in POSITIONS:
            identity=self.meta["current"][p]["blocks"][b]
            old,new=self.originals[identity],self.document["pages"][p]["blocks"][b]
            self.assertTrue(require_source_format(old,new),identity)
            field=source_slot(old)
            self.assertEqual([line for line in old[field].splitlines() if line.strip()],
                             [line for line in new[field].splitlines() if line.strip()],identity)
            self.assertGreater(new[field].count("\n\n"),old[field].count("\n\n"),identity)
            for key in ("answers","outputCheck","correctOrder","projectFiles","compileCheck"):
                self.assertEqual(old.get(key),new.get(key),identity+":"+key)
            previous["pages"][p]["blocks"][b]=deepcopy(old)
        state.validate(previous)
        state.validate_transition(previous,self.document)
        self.assertEqual(state.validate(previous),state.validate(self.document))
        for field in ("origin","family","current","additions"):
            self.assertEqual(previous["stateLayout"][field],self.meta[field])
        self.assertTrue(self.document["compilerPuzzlesRequired"])

    def test_tampering_with_original_source_history_fails(self):
        fake=deepcopy(self.document)
        fake["stateLayout"]["sourceFormatRevisions"][0]["block"]["code"] += "\nint unintended_change;\n"
        with self.assertRaises(state.LayoutError):
            state.validate(fake)

    def test_cpp23_before_after_outputs_and_optional_cases(self):
        with tempfile.TemporaryDirectory(prefix="u6l6-mutable-reference-") as folder:
            for index,(p,b) in enumerate(POSITIONS):
                identity=self.meta["current"][p]["blocks"][b]
                before=self.originals[identity]
                after=self.document["pages"][p]["blocks"][b]
                cases=after.get("outputCheck",{}).get("cases",[])
                inputs=[case["standardInput"] for case in cases] or [""]
                variants=[]
                for tag,block in (("original",before),("formatted",after)):
                    source=Path(folder)/f"{index}-{tag}.cpp"
                    executable=Path(folder)/f"{index}-{tag}"
                    source.write_text(runnable_code(block),encoding="utf-8")
                    built=subprocess.run(["c++","-std=c++23","-O0","-o",str(executable),str(source)],
                                         capture_output=True,text=True,timeout=90)
                    self.assertEqual(0,built.returncode,identity+built.stderr)
                    runs=[]
                    for data in inputs:
                        execution=subprocess.run([str(executable)],input=data,
                                                 capture_output=True,text=True,timeout=15)
                        self.assertEqual(0,execution.returncode,identity+execution.stderr)
                        self.assertEqual("",execution.stderr,identity)
                        runs.append((execution.returncode,execution.stdout,execution.stderr))
                    variants.append(runs)
                self.assertEqual(variants[0],variants[1],identity)
                if after["type"] == "output_prediction":
                    self.assertIn(variants[1][0][1].rstrip("\n"),after["answers"],identity)
                if after["type"] == "fill_code":
                    self.assertEqual(["&"],after["answers"],identity)
                    self.assertEqual("0",variants[1][0][1],identity)
                if cases:
                    for output,case in zip(variants[1],cases):
                        if (p,b) in UNSOLVED:
                            self.assertNotIn(output[1],case["outputs"],identity)
                        else:
                            self.assertIn(output[1],case["outputs"],identity)
                if (p,b)==(5,10):
                    self.assertIn("Elementos: 4\n-2\n4\n-1\n6",variants[1][0][1])
        self.assertEqual(3,len(self.document["pages"][5]["blocks"][7]["outputCheck"]["cases"]))
        self.assertEqual(5,len(self.document["pages"][8]["blocks"][8]["outputCheck"]["cases"]))
        self.assertEqual(2,len(self.document["pages"][13]["blocks"][7]["outputCheck"]["cases"]))
        self.assertEqual(4,len(self.document["pages"][14]["blocks"][7]["outputCheck"]["cases"]))

if __name__=="__main__":
    unittest.main()
