"""U5L8: minimum, maximum, ties and deliberately incomplete editors."""
from copy import deepcopy
import json
from pathlib import Path
import subprocess
import tempfile
import unittest
import stable_state_layout as state
from code_format_compat import require_source_format

ROOT=Path(__file__).resolve().parents[2]
PATH=ROOT/"app/src/main/assets/content/es/programming/cpp_fundamentals/colecciones_y_recorridos/lesson_extremos_sin_inventar_datos.json"
POSITIONS=((0,2),(1,2),(2,1),(2,3),(3,1),(3,2),(4,1),(5,2),(6,2),(6,5),
           (7,1),(7,2),(7,7),(7,10),(7,13),(7,16),(7,19))
UNSOLVED={(6,2),(6,5),(7,10),(7,13)}

def field(block):
    return "initialCode" if block["type"]=="cpp_compiler" else "code"

def runnable(block):
    source=block[field(block)]
    if block["type"]=="fill_code":
        assert source.count("___")==1
        source=source.replace("___",block["answers"][0])
    return source

class ExtremosFormatBatch(unittest.TestCase):
    def setUp(self):
        self.current=json.loads(PATH.read_text(encoding="utf-8"))
        self.meta=self.current["stateLayout"]
        self.previous={record["id"]:record["block"] for record in self.meta["sourceFormatRevisions"]}

    def test_preserves_lexical_content_and_historical_progress(self):
        state.validate(self.current)
        self.assertEqual(len(POSITIONS),len(self.previous))
        self.assertEqual([self.meta["current"][p]["blocks"][b] for p,b in POSITIONS],
                         [entry["id"] for entry in self.meta["editorial"][-len(POSITIONS):]])
        old=deepcopy(self.current)
        old["stateLayout"].pop("sourceFormatRevisions")
        old["stateLayout"]["editorial"]=old["stateLayout"]["editorial"][:-len(POSITIONS)]
        for p,b in POSITIONS:
            identity=self.meta["current"][p]["blocks"][b]
            before,after=self.previous[identity],self.current["pages"][p]["blocks"][b]
            self.assertTrue(require_source_format(before,after),identity)
            source_field=field(before)
            self.assertEqual([line for line in before[source_field].splitlines() if line.strip()],
                             [line for line in after[source_field].splitlines() if line.strip()],identity)
            self.assertEqual(before[source_field].count("} else {"),
                             after[source_field].count("} else {"),identity)
            old["pages"][p]["blocks"][b]=deepcopy(before)
        state.validate(old)
        state.validate_transition(old,self.current)
        self.assertEqual(state.validate(old),state.validate(self.current))
        for key in ("origin","family","current","additions"):
            self.assertEqual(old["stateLayout"][key],self.meta[key])

    def test_cpp23_has_identical_results_for_all_original_and_optional_cases(self):
        with tempfile.TemporaryDirectory(prefix="extremos-format-") as folder:
            for index,(p,b) in enumerate(POSITIONS):
                identity=self.meta["current"][p]["blocks"][b]
                previous,current=self.previous[identity],self.current["pages"][p]["blocks"][b]
                cases=current.get("outputCheck",{}).get("cases",[])
                inputs=[case["standardInput"] for case in cases] or [current.get("initialStandardInput","")]
                before_and_after=[]
                for tag,block in (("original",previous),("formatted",current)):
                    src=Path(folder)/f"{index}-{tag}.cpp"
                    exe=Path(folder)/f"{index}-{tag}"
                    src.write_text(runnable(block),encoding="utf-8")
                    built=subprocess.run(["c++","-std=c++23","-O0","-o",str(exe),str(src)],
                                         capture_output=True,text=True,timeout=90)
                    self.assertEqual(0,built.returncode,identity+built.stderr)
                    outputs=[]
                    for stdin in inputs:
                        run=subprocess.run([str(exe)],input=stdin,capture_output=True,
                                           text=True,timeout=15)
                        self.assertEqual(0,run.returncode,identity)
                        self.assertEqual("",run.stderr,identity)
                        outputs.append((run.returncode,run.stdout,run.stderr))
                    before_and_after.append(outputs)
                self.assertEqual(before_and_after[0],before_and_after[1],identity)
                if current["type"]=="output_prediction":
                    self.assertIn(before_and_after[1][0][1],current["answers"],identity)
                if (p,b) in UNSOLVED:
                    self.assertTrue(cases,identity)
                    self.assertNotIn(before_and_after[1][0][1],cases[0]["outputs"],identity)
                else:
                    for observed,case in zip(before_and_after[1],cases):
                        self.assertIn(observed[1],case["outputs"],identity)

if __name__=="__main__":
    unittest.main()
