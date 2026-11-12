"""U5L10: sorted copies, duplicates and student editors retain historical state."""
from copy import deepcopy
import json
from pathlib import Path
import subprocess
import tempfile
import unittest

import stable_state_layout as state
from code_format_compat import require_source_format

ROOT=Path(__file__).resolve().parents[2]
LESSON=ROOT/"app/src/main/assets/content/es/programming/cpp_fundamentals/colecciones_y_recorridos/lesson_ordenar_y_conservar_el_original.json"
POSITIONS=((0,3),(1,1),(1,3),(2,1),(2,2),(3,2),(4,2),(5,2),(6,1),(6,3),
           (7,2),(7,5),(8,1),(8,2),(8,7),(8,10),(8,13),(8,16),(8,19))
UNSOLVED={(7,2),(7,5),(8,10),(8,13)}

def source_field(block):
    return "initialCode" if block["type"]=="cpp_compiler" else ("template" if block["type"]=="inline_fill_code" else "code")

def compilable(block):
    src=block[source_field(block)]
    if block["type"]=="fill_code":
        assert src.count("___")==1
        src=src.replace("___",block["answers"][0],1)
    if block["type"]=="inline_fill_code":
        for answer in block["answers"]:
            assert "[]" in src
            src=src.replace("[]",answer,1)
        assert "[]" not in src
    return src

class SortingFormatBatch(unittest.TestCase):
    def setUp(self):
        self.current=json.loads(LESSON.read_text(encoding="utf-8"))
        self.meta=self.current["stateLayout"]
        self.originals={x["id"]:x["block"] for x in self.meta["sourceFormatRevisions"]}

    def test_authenticated_historical_state_and_answers(self):
        state.validate(self.current)
        self.assertEqual(len(POSITIONS),len(self.originals))
        ids=[self.meta["current"][p]["blocks"][b] for p,b in POSITIONS]
        self.assertEqual(ids,[x["id"] for x in self.meta["editorial"][-len(POSITIONS):]])
        old=deepcopy(self.current)
        old["stateLayout"].pop("sourceFormatRevisions")
        old["stateLayout"]["editorial"]=old["stateLayout"]["editorial"][:-len(POSITIONS)]
        for p,b in POSITIONS:
            ident=self.meta["current"][p]["blocks"][b]
            original,current=self.originals[ident],self.current["pages"][p]["blocks"][b]
            self.assertTrue(require_source_format(original,current),ident)
            field=source_field(original)
            self.assertEqual([line for line in original[field].splitlines() if line.strip()],
                             [line for line in current[field].splitlines() if line.strip()],ident)
            self.assertEqual(original[field].count("std::sort("),
                             current[field].count("std::sort("),ident)
            old["pages"][p]["blocks"][b]=deepcopy(original)
        state.validate(old)
        state.validate_transition(old,self.current)
        self.assertEqual(state.validate(old),state.validate(self.current))
        for key in ("origin","family","current","additions"):
            self.assertEqual(old["stateLayout"][key],self.meta[key])

    def test_cpp23_equivalent_outputs_and_unsolved_editors(self):
        with tempfile.TemporaryDirectory(prefix="u5l10-sort-") as folder:
            for index,(p,b) in enumerate(POSITIONS):
                ident=self.meta["current"][p]["blocks"][b]
                original,current=self.originals[ident],self.current["pages"][p]["blocks"][b]
                cases=current.get("outputCheck",{}).get("cases",[])
                inputs=[case["standardInput"] for case in cases] or [
                    current.get("initialStandardInput","")]
                results=[]
                for tag,block in (("before",original),("after",current)):
                    src=Path(folder)/f"{index}-{tag}.cpp"
                    binary=Path(folder)/f"{index}-{tag}"
                    src.write_text(compilable(block),encoding="utf-8")
                    built=subprocess.run(["c++","-std=c++23","-O0","-o",str(binary),str(src)],
                                         capture_output=True,text=True,timeout=90)
                    self.assertEqual(0,built.returncode,ident+built.stderr)
                    runs=[]
                    for stdin in inputs:
                        run=subprocess.run([str(binary)],input=stdin,
                                            capture_output=True,text=True,timeout=15)
                        self.assertEqual(0,run.returncode,ident)
                        self.assertEqual("",run.stderr,ident)
                        runs.append((run.returncode,run.stdout,run.stderr))
                    results.append(runs)
                self.assertEqual(results[0],results[1],ident)
                if current["type"]=="output_prediction":
                    self.assertIn(results[1][0][1],current["answers"],ident)
                if (p,b) in UNSOLVED:
                    self.assertTrue(cases,ident)
                    self.assertNotIn(results[1][0][1],cases[0]["outputs"],ident)
                else:
                    for output,case in zip(results[1],cases):
                        self.assertIn(output[1],case["outputs"],ident)

if __name__=="__main__":
    unittest.main()
