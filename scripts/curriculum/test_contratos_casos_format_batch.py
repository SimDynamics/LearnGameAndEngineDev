"""U6L7: preserve contracts, domain validation, historic state, and C++23 outputs."""
from copy import deepcopy
import json
from pathlib import Path
import subprocess
import tempfile
import unittest

import stable_state_layout as state
from code_format_compat import require_source_format

ROOT = Path(__file__).resolve().parents[2]
PATH = ROOT / "app/src/main/assets/content/es/programming/cpp_fundamentals/funciones_y_contratos/lesson_contratos_y_casos_de_prueba.json"
POSITIONS = ((0,2),(1,2),(2,2),(3,2),(3,7),(3,10),(5,3))
EXPECTED_INPUTS = ("0 6","120 12","25 0","-1 6")

def source_slot(block):
    return "initialCode" if block["type"] == "cpp_compiler" else "code"

class ContractCasesFormatBatch(unittest.TestCase):
    def setUp(self):
        self.current=json.loads(PATH.read_text(encoding="utf-8"))
        self.meta=self.current["stateLayout"]
        self.originals={entry["id"]:entry["block"]
                        for entry in self.meta["sourceFormatRevisions"]}

    def test_identity_and_original_source_semantics(self):
        state.validate(self.current)
        self.assertEqual(7,len(POSITIONS))
        self.assertEqual(len(POSITIONS),len(self.originals))
        ids=[self.meta["current"][p]["blocks"][b] for p,b in POSITIONS]
        self.assertEqual(ids,[entry["id"] for entry in self.meta["editorial"][-len(POSITIONS):]])
        before=deepcopy(self.current)
        before["stateLayout"].pop("sourceFormatRevisions")
        before["stateLayout"]["editorial"]=before["stateLayout"]["editorial"][:-len(POSITIONS)]
        for p,b in POSITIONS:
            identity=self.meta["current"][p]["blocks"][b]
            old,new=self.originals[identity],self.current["pages"][p]["blocks"][b]
            self.assertTrue(require_source_format(old,new),identity)
            slot=source_slot(old)
            self.assertEqual([line for line in old[slot].splitlines() if line.strip()],
                             [line for line in new[slot].splitlines() if line.strip()],identity)
            self.assertGreater(new[slot].count("\n\n"),old[slot].count("\n\n"),identity)
            self.assertEqual(old.get("outputCheck"),new.get("outputCheck"),identity)
            self.assertEqual(old.get("answers"),new.get("answers"),identity)
            self.assertEqual(old.get("compileCheck"),new.get("compileCheck"),identity)
            before["pages"][p]["blocks"][b]=deepcopy(old)
        state.validate(before)
        state.validate_transition(before,self.current)
        self.assertEqual(state.validate(before),state.validate(self.current))
        for field in ("origin","family","current","additions"):
            self.assertEqual(before["stateLayout"][field],self.meta[field])
        self.assertTrue(self.current["compilerPuzzlesRequired"])
        cases=self.current["pages"][3]["blocks"][7]["outputCheck"]["cases"]
        self.assertEqual(EXPECTED_INPUTS,tuple(case["standardInput"] for case in cases))

    def test_mutated_archive_cannot_forge_progress(self):
        fake=deepcopy(self.current)
        fake["stateLayout"]["sourceFormatRevisions"][0]["block"]["code"]+="\nint forged;\n"
        with self.assertRaises(state.LayoutError):
            state.validate(fake)

    def test_cpp23_original_formatted_and_all_optional_cases(self):
        with tempfile.TemporaryDirectory(prefix="u6l7-contract-case-") as folder:
            for i,(p,b) in enumerate(POSITIONS):
                identity=self.meta["current"][p]["blocks"][b]
                old,new=self.originals[identity],self.current["pages"][p]["blocks"][b]
                cases=new.get("outputCheck",{}).get("cases",[])
                inputs=[case["standardInput"] for case in cases] or [""]
                runs=[]
                for label,block in (("old",old),("new",new)):
                    source=Path(folder)/f"{i}-{label}.cpp"
                    exe=Path(folder)/f"{i}-{label}"
                    source.write_text(block[source_slot(block)],encoding="utf-8")
                    built=subprocess.run(["c++","-std=c++23","-O0",str(source),"-o",str(exe)],
                                         capture_output=True,text=True,timeout=90)
                    self.assertEqual(0,built.returncode,identity+built.stderr)
                    results=[]
                    for data in inputs:
                        execution=subprocess.run([str(exe)],input=data,capture_output=True,
                                                 text=True,timeout=15)
                        self.assertEqual(0,execution.returncode,identity+execution.stderr)
                        self.assertEqual("",execution.stderr,identity)
                        results.append((execution.returncode,execution.stdout,execution.stderr))
                    runs.append(results)
                self.assertEqual(runs[0],runs[1],identity)
                if new["type"]=="output_prediction":
                    self.assertIn(runs[1][0][1].rstrip("\n"),new["answers"],identity)
                if cases:
                    for result,case in zip(runs[1],cases):
                        self.assertIn(result[1],case["outputs"],identity)
                if (p,b)==(3,7):
                    self.assertEqual(["0 0 1","10 0 1","Fuera del dominio","Fuera del dominio"],
                                     [item[1].rstrip("\n") for item in runs[1]])
                if (p,b)==(3,10):
                    self.assertEqual("5 1 0",runs[1][0][1].rstrip("\n"),identity)
                    self.assertNotEqual("4 1 1",runs[1][0][1].rstrip("\n"),identity)

if __name__=="__main__":
    unittest.main()
