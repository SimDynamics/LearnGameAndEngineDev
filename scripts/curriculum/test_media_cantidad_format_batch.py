"""U5L9: guarded mean on empty vectors, floating point and unchanged tasks."""
from copy import deepcopy
import json
from pathlib import Path
import subprocess
import tempfile
import unittest
import stable_state_layout as state
from code_format_compat import require_source_format

ROOT=Path(__file__).resolve().parents[2]
PATH=ROOT/"app/src/main/assets/content/es/programming/cpp_fundamentals/colecciones_y_recorridos/lesson_media_y_cantidad_de_datos.json"
POSITIONS=((0,3),(1,1),(1,3),(2,1),(2,2),(3,1),(4,2),(5,2),
           (6,2),(6,5),(7,1),(7,2),(7,7),(7,10),(7,13))
UNSOLVED={(6,2),(6,5),(7,10)}

def slot(block):
    return "initialCode" if block["type"]=="cpp_compiler" else "code"

def runnable(block):
    source=block[slot(block)]
    if block["type"]=="fill_code":
        assert source.count("___")==1
        source=source.replace("___",block["answers"][0])
    return source

class MeanFormatHistory(unittest.TestCase):
    def setUp(self):
        self.current=json.loads(PATH.read_text(encoding="utf-8"))
        self.meta=self.current["stateLayout"]
        self.archived={r["id"]:r["block"] for r in self.meta["sourceFormatRevisions"]}

    def test_only_physical_spacing_changes_and_no_progress_reset(self):
        state.validate(self.current)
        self.assertEqual(len(POSITIONS),len(self.archived))
        self.assertEqual([self.meta["current"][p]["blocks"][b] for p,b in POSITIONS],
                         [x["id"] for x in self.meta["editorial"][-len(POSITIONS):]])
        before=deepcopy(self.current)
        before["stateLayout"].pop("sourceFormatRevisions")
        before["stateLayout"]["editorial"]=before["stateLayout"]["editorial"][:-len(POSITIONS)]
        for p,b in POSITIONS:
            identity=self.meta["current"][p]["blocks"][b]
            old,new=self.archived[identity],self.current["pages"][p]["blocks"][b]
            self.assertTrue(require_source_format(old,new),identity)
            source_slot=slot(old)
            self.assertEqual([l for l in old[source_slot].splitlines() if l.strip()],
                             [l for l in new[source_slot].splitlines() if l.strip()],identity)
            self.assertEqual(old[source_slot].count("} else {"),
                             new[source_slot].count("} else {"),identity)
            before["pages"][p]["blocks"][b]=deepcopy(old)
        state.validate(before)
        state.validate_transition(before,self.current)
        self.assertEqual(state.validate(before),state.validate(self.current))
        for field in ("origin","family","current","additions"):
            self.assertEqual(before["stateLayout"][field],self.meta[field])

    def test_same_cpp23_outputs_and_initial_programs_still_need_student_work(self):
        with tempfile.TemporaryDirectory(prefix="mean-format-") as folder:
            for ordinal,(p,b) in enumerate(POSITIONS):
                identity=self.meta["current"][p]["blocks"][b]
                original,updated=self.archived[identity],self.current["pages"][p]["blocks"][b]
                cases=updated.get("outputCheck",{}).get("cases",[])
                inputs=[x["standardInput"] for x in cases] or [updated.get("initialStandardInput","")]
                histories=[]
                for version,block in (("old",original),("formatted",updated)):
                    source=Path(folder)/f"{ordinal}-{version}.cpp"
                    executable=Path(folder)/f"{ordinal}-{version}"
                    source.write_text(runnable(block),encoding="utf-8")
                    built=subprocess.run(["c++","-std=c++23","-O0","-o",str(executable),str(source)],
                                         capture_output=True,text=True,timeout=90)
                    self.assertEqual(0,built.returncode,identity+built.stderr)
                    observations=[]
                    for stdin in inputs:
                        run=subprocess.run([str(executable)],input=stdin,capture_output=True,
                                           text=True,timeout=15)
                        self.assertEqual(0,run.returncode,identity)
                        self.assertEqual("",run.stderr,identity)
                        observations.append((run.returncode,run.stdout,run.stderr))
                    histories.append(observations)
                self.assertEqual(histories[0],histories[1],identity)
                if updated["type"]=="output_prediction":
                    self.assertIn(histories[1][0][1],updated["answers"],identity)
                if (p,b) in UNSOLVED:
                    self.assertNotIn(histories[1][0][1],cases[0]["outputs"],identity)
                else:
                    for observation,case in zip(histories[1],cases):
                        self.assertIn(observation[1],case["outputs"],identity)

if __name__=="__main__":
    unittest.main()
