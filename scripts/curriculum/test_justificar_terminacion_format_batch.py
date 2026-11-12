"""U4L8: separar while, do-while y decisiones sin alterar los objetivos."""
from copy import deepcopy
import json
from pathlib import Path
import subprocess
import tempfile
import unittest

import stable_state_layout as state
from code_format_compat import require_source_format

ROOT = Path(__file__).resolve().parents[2]
PATH = ROOT / "app/src/main/assets/content/es/programming/cpp_fundamentals/repeticion_y_terminacion/lesson_justificar_terminacion_y_elegir_bucle.json"
POSITIONS = ((0,3),(0,5),(1,1),(1,2),(1,4),(2,3),
             (3,2),(3,3),(3,4),(4,3),(5,2),(5,6))
INCOMPLETE_EDITOR = (4,3)


def source_field(block):
    return "initialCode" if block["type"]=="cpp_compiler" else "code"


def runnable(block):
    source = block[source_field(block)]
    if block["type"]=="fill_code":
        assert source.count("___")==1
        source=source.replace("___",block["answers"][0],1)
    return source


class TerminationFormatBatch(unittest.TestCase):
    def setUp(self):
        self.current=json.loads(PATH.read_text(encoding="utf-8"))
        self.meta=self.current["stateLayout"]
        self.originals={x["id"]:x["block"] for x in self.meta["sourceFormatRevisions"]}

    def test_exact_historical_state_and_source_only_edits(self):
        state.validate(self.current)
        self.assertEqual(len(POSITIONS),len(self.originals))
        ids=[self.meta["current"][p]["blocks"][b] for p,b in POSITIONS]
        self.assertEqual(ids,[x["id"] for x in self.meta["editorial"][-len(POSITIONS):]])
        previous=deepcopy(self.current)
        previous["stateLayout"].pop("sourceFormatRevisions")
        previous["stateLayout"]["editorial"]=previous["stateLayout"]["editorial"][:-len(POSITIONS)]
        for p,b in POSITIONS:
            identity=self.meta["current"][p]["blocks"][b]
            old,new=self.originals[identity],self.current["pages"][p]["blocks"][b]
            self.assertTrue(require_source_format(old,new),identity)
            key=source_field(old)
            self.assertEqual([line for line in old[key].splitlines() if line.strip()],
                             [line for line in new[key].splitlines() if line.strip()],identity)
            self.assertEqual(old[key].count("} while ("),new[key].count("} while ("),identity)
            self.assertEqual(old[key].count("} else {"),new[key].count("} else {"),identity)
            previous["pages"][p]["blocks"][b]=deepcopy(old)
        state.validate(previous)
        state.validate_transition(previous,self.current)
        self.assertEqual(state.validate(previous),state.validate(self.current))
        for key in ("origin","family","current","additions"):
            self.assertEqual(previous["stateLayout"][key],self.meta[key])

    def test_cxx23_results_before_and_after_match_all_case_inputs(self):
        with tempfile.TemporaryDirectory(prefix="u4l8-format-") as folder:
            for index,(p,b) in enumerate(POSITIONS):
                identity=self.meta["current"][p]["blocks"][b]
                before,after=self.originals[identity],self.current["pages"][p]["blocks"][b]
                cases=after.get("outputCheck",{}).get("cases",[])
                inputs=[x["standardInput"] for x in cases] or [after.get("initialStandardInput","")]
                results=[]
                for label,block in (("before",before),("after",after)):
                    src=Path(folder)/f"{index}-{label}.cpp"
                    exe=Path(folder)/f"{index}-{label}"
                    src.write_text(runnable(block),encoding="utf-8")
                    build=subprocess.run(["c++","-std=c++23","-O0","-o",str(exe),str(src)],
                                         capture_output=True,text=True,timeout=90)
                    self.assertEqual(0,build.returncode,identity+build.stderr)
                    runs=[]
                    for stdin in inputs:
                        run=subprocess.run([str(exe)],input=stdin,
                                           capture_output=True,text=True,timeout=15)
                        self.assertEqual(0,run.returncode,identity)
                        self.assertEqual("",run.stderr,identity)
                        runs.append((run.returncode,run.stdout,run.stderr))
                    results.append(runs)
                self.assertEqual(results[0],results[1],identity)
                if after["type"]=="output_prediction":
                    self.assertIn(results[1][0][1],after["answers"],identity)
                if (p,b)==INCOMPLETE_EDITOR:
                    self.assertNotIn(results[1][0][1],cases[0]["outputs"],identity)
                else:
                    for output,case in zip(results[1],cases):
                        self.assertIn(output[1],case["outputs"],identity)


if __name__=="__main__":
    unittest.main()
