"""U5L5: index bounds, reverse loop, authentic state and two unsolved programs."""
from copy import deepcopy
import json
from pathlib import Path
import subprocess
import tempfile
import unittest

import stable_state_layout as state
from code_format_compat import require_source_format

ROOT=Path(__file__).resolve().parents[2]
PATH=ROOT/"app/src/main/assets/content/es/programming/cpp_fundamentals/colecciones_y_recorridos/lesson_recorrer_indices_con_seguridad.json"
POSITIONS=((0,3),(1,1),(1,2),(2,1),(2,2),(3,2),(3,4),(4,2),
           (5,1),(6,2),(7,1),(7,2),(7,7),(7,10),(7,13))
INCOMPLETE={(6,2),(7,10)}


def field(block):
    if block["type"]=="cpp_compiler":return "initialCode"
    if block["type"]=="inline_fill_code":return "template"
    return "code"


def runnable(block):
    src=block[field(block)]
    if block["type"]=="fill_code":
        assert src.count("___")==1
        src=src.replace("___",block["answers"][0])
    if block["type"]=="inline_fill_code":
        for answer in block["answers"]:
            assert "[]" in src
            src=src.replace("[]",answer,1)
    return src


class SafeIndexLoopsFormat(unittest.TestCase):
    def setUp(self):
        self.doc=json.loads(PATH.read_text(encoding="utf-8"))
        self.meta=self.doc["stateLayout"]
        self.snapshots={x["id"]:x["block"] for x in self.meta["sourceFormatRevisions"]}

    def test_exact_source_transition_preserves_index_logic_and_answers(self):
        state.validate(self.doc)
        self.assertEqual(len(POSITIONS),len(self.snapshots))
        ids=[self.meta["current"][p]["blocks"][b] for p,b in POSITIONS]
        self.assertEqual(ids,[proof["id"] for proof in self.meta["editorial"][-len(POSITIONS):]])
        previous=deepcopy(self.doc)
        previous["stateLayout"].pop("sourceFormatRevisions")
        previous["stateLayout"]["editorial"]=previous["stateLayout"]["editorial"][:-len(POSITIONS)]
        for p,b in POSITIONS:
            identity=self.meta["current"][p]["blocks"][b]
            before,after=self.snapshots[identity],self.doc["pages"][p]["blocks"][b]
            self.assertTrue(require_source_format(before,after),identity)
            slot=field(before)
            self.assertEqual([line for line in before[slot].splitlines() if line.strip()],
                             [line for line in after[slot].splitlines() if line.strip()],identity)
            previous["pages"][p]["blocks"][b]=deepcopy(before)
        state.validate(previous)
        state.validate_transition(previous,self.doc)
        self.assertEqual(state.validate(previous),state.validate(self.doc))
        for key in ("origin","family","current","additions"):
            self.assertEqual(previous["stateLayout"][key],self.meta[key])

    def test_cpp23_all_cases_keep_same_output_and_initial_challenge(self):
        with tempfile.TemporaryDirectory(prefix="safe-index-format-") as folder:
            for i,(p,b) in enumerate(POSITIONS):
                identity=self.meta["current"][p]["blocks"][b]
                before,after=self.snapshots[identity],self.doc["pages"][p]["blocks"][b]
                cases=after.get("outputCheck",{}).get("cases",[])
                inputs=[item["standardInput"] for item in cases] or [
                    after.get("initialStandardInput","")]
                executions=[]
                for tag,block in (("old",before),("new",after)):
                    src=Path(folder)/f"{i}-{tag}.cpp"
                    executable=Path(folder)/f"{i}-{tag}"
                    src.write_text(runnable(block),encoding="utf-8")
                    built=subprocess.run(["c++","-std=c++23","-O0","-o",str(executable),str(src)],
                                         capture_output=True,text=True,timeout=90)
                    self.assertEqual(0,built.returncode,identity+built.stderr)
                    runs=[]
                    for stdin in inputs:
                        run=subprocess.run([str(executable)],input=stdin,
                                           capture_output=True,text=True,timeout=15)
                        self.assertEqual(0,run.returncode,identity)
                        self.assertEqual("",run.stderr,identity)
                        runs.append((run.returncode,run.stdout,run.stderr))
                    executions.append(runs)
                self.assertEqual(executions[0],executions[1],identity)
                if after["type"]=="output_prediction":
                    self.assertIn(executions[1][0][1],after["answers"],identity)
                if (p,b) in INCOMPLETE:
                    self.assertNotIn(executions[1][0][1],cases[0]["outputs"],identity)
                else:
                    for result,case in zip(executions[1],cases):
                        self.assertIn(result[1],case["outputs"],identity)


if __name__=="__main__":
    unittest.main()
