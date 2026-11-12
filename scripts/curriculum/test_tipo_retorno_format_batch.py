"""U6L12: verify original code tokens, C++23 behavior, and optional input cases."""
from copy import deepcopy
import json
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest

import stable_state_layout as state
from code_format_compat import require_source_format

ROOT=Path(__file__).resolve().parents[2]
PATH=ROOT/"app/src/main/assets/content/es/programming/cpp_fundamentals/funciones_y_contratos/lesson_tipo_de_retorno_explicito_y_deducido.json"
INTENTIONALLY_INVALID={(4,2),(5,2)}

class ReturnTypeFormatBatch(unittest.TestCase):
    def setUp(self):
        self.doc=json.loads(PATH.read_text(encoding="utf-8"))
        self.revisions={x["id"]:x["block"] for x in self.doc["stateLayout"]["sourceFormatRevisions"]}

    def test_immutable_contracts_and_source_tokens(self):
        state.validate(self.doc)
        self.assertGreaterEqual(len(self.revisions),10)
        self.assertEqual(len(self.revisions),len(self.doc["stateLayout"]["sourceFormatRevisions"]))
        for p,page in enumerate(self.doc["pages"]):
            for b,block in enumerate(page["blocks"]):
                identity=self.doc["stateLayout"]["current"][p]["blocks"][b]
                old=self.revisions.get(identity)
                if old is None: continue
                self.assertTrue(require_source_format(old,block),identity)
                field="initialCode" if block["type"]=="cpp_compiler" else "code"
                self.assertEqual([line for line in old[field].splitlines() if line.strip()],
                                 [line for line in block[field].splitlines() if line.strip()],identity)
                self.assertGreater(block[field].count("\n\n"),old[field].count("\n\n"),identity)
                for key in ("outputCheck","compileCheck","answers"):
                    self.assertEqual(old.get(key),block.get(key),identity)
        self.assertEqual(11,len(self.doc["pages"]))
        self.assertTrue(self.doc["compilerPuzzlesRequired"])

    def test_one_required_case_other_cases_optional(self):
        for p,b in ((6,2),(6,11),(7,6),(8,4),(8,17),(9,6)):
            paragraph=self.doc["pages"][p]["blocks"][b]["text"]
            self.assertIn("primer caso",paragraph)
            self.assertIn("pruebas opcionales",paragraph)
            self.assertIn("no bloquean el paso",paragraph)
        scalar=self.doc["pages"][6]["blocks"][12]["outputCheck"]["cases"]
        collection=self.doc["pages"][8]["blocks"][18]["outputCheck"]["cases"]
        self.assertEqual(3,len(scalar))
        self.assertEqual(9,len(collection))
        self.assertEqual("-4 3",scalar[0]["standardInput"])
        self.assertEqual("0 4",collection[0]["standardInput"])
        for p,indices,cases in ((6,range(8,11),scalar),(8,range(8,17),collection)):
            descriptions="\n".join(self.doc["pages"][p]["blocks"][i]["text"] for i in indices)
            for case in cases:
                self.assertIn("Entrada: `"+case["standardInput"]+"`",descriptions)
                self.assertIn("`"+case["outputs"][0].split("\n")[0]+"`",descriptions)

    @unittest.skipUnless(shutil.which("c++"),"C++ compiler unavailable")
    def test_cxx23_runs_before_and_after_equal(self):
        with tempfile.TemporaryDirectory(prefix="u6l12-returns-") as folder:
            for p,page in enumerate(self.doc["pages"]):
                for b,block in enumerate(page["blocks"]):
                    if (p,b) in INTENTIONALLY_INVALID: continue
                    identity=self.doc["stateLayout"]["current"][p]["blocks"][b]
                    old=self.revisions.get(identity)
                    if old is None: continue
                    key="initialCode" if block["type"]=="cpp_compiler" else "code"
                    variants=[]
                    cases=block.get("outputCheck",{}).get("cases",[])
                    inputs=[c["standardInput"] for c in cases] or [""]
                    for label,item in (("before",old),("after",block)):
                        source=Path(folder)/f"{p}-{b}-{label}.cpp"
                        exe=Path(folder)/f"{p}-{b}-{label}"
                        source.write_text(item[key],encoding="utf-8")
                        compile_result=subprocess.run(["c++","-std=c++23","-O0",str(source),"-o",str(exe)],
                                                      capture_output=True,text=True,timeout=90)
                        self.assertEqual(0,compile_result.returncode,identity+compile_result.stderr)
                        runs=[]
                        for data in inputs:
                            run=subprocess.run([str(exe)],input=data,capture_output=True,text=True,timeout=15)
                            runs.append((run.returncode,run.stdout,run.stderr))
                        variants.append(runs)
                    self.assertEqual(variants[0],variants[1],identity)

if __name__=="__main__":
    unittest.main()
