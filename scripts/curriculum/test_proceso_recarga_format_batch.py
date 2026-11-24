"""Regresión de recarga: separación física y estado histórico intactos."""
from copy import deepcopy
import json
from pathlib import Path
import subprocess
from windows_cpp_ci import msvc_cpp23_command, windows_executable
import tempfile
import unittest

import stable_state_layout as state
from code_format_compat import require_source_format
from format_batch_history import (
    Publication, all_owners, assert_format_history, assert_stable_storage,
    load_publication,
)

ROOT=Path(__file__).resolve().parents[2]
PATH=ROOT/"app/src/main/assets/content/es/programming/cpp_fundamentals/repeticion_y_terminacion/lesson_construir_un_proceso_de_recarga.json"
PUBLICATIONS={
    "before": Publication("04bb93bf6e4278b3669118b5f2e8216c60ef5cd9",
        "4c48fac1ddb139736a23dad943e9705f3d8bd63af773bcd5fd6cd72782836212"),
    "formatted": Publication("64887233e298bda091007c92df79e2bac2834059",
        "92385e342f0b73d4123513148785ea8acf65673fb13393cfd16ce9dfeba3ea0f"),
}
POSITIONS=((1,3),(2,1),(2,7),(3,1),(3,3),(3,8))
STARTERS={(1,3),(3,8)}


def slot(block):
    return "initialCode" if block["type"]=="cpp_compiler" else "code"


class RechargeFormatBatch(unittest.TestCase):
    def setUp(self):
        self.after=json.loads(PATH.read_text(encoding="utf-8"))
        self.meta=self.after["stateLayout"]
        self.history={x["id"]:x["block"] for x in self.meta["sourceFormatRevisions"]}

    def test_authenticated_transition_preserves_ids_answers_and_inputs(self):
        relative=PATH.relative_to(ROOT).as_posix()
        previous_publication=load_publication(ROOT,relative,PUBLICATIONS["before"])
        formatted=load_publication(ROOT,relative,PUBLICATIONS["formatted"])
        for document in (previous_publication,formatted,self.after):
            state.validate(document)
        published=formatted["stateLayout"]
        ids=[published["current"][p]["blocks"][b] for p,b in POSITIONS]
        archives=published["sourceFormatRevisions"]
        self.assertEqual(len(POSITIONS),len(archives))
        self.assertEqual(ids,[entry["id"] for entry in archives])
        prefix=previous_publication["stateLayout"]["editorial"]
        self.assertEqual(published["editorial"][:len(prefix)],prefix)
        self.assertEqual(ids,[entry["id"] for entry in published["editorial"][len(prefix):]])
        # Pin this batch to its complete publication; later compatible edits append history.
        self.assertEqual(self.meta["editorial"][:len(published["editorial"])],
                         published["editorial"])
        self.assertEqual(self.meta["sourceFormatRevisions"][:len(archives)],archives)
        before_blocks=all_owners(previous_publication)
        for p,b in POSITIONS:
            identity=published["current"][p]["blocks"][b]
            before,new=before_blocks[identity],formatted["pages"][p]["blocks"][b]
            self.assertTrue(require_source_format(before,new),identity)
            key=slot(before)
            self.assertEqual([line for line in before[key].splitlines() if line.strip()],
                             [line for line in new[key].splitlines() if line.strip()],identity)
            self.assertEqual(before[key].count("} else {"),new[key].count("} else {"),identity)
        state.validate_transition(previous_publication,formatted)
        state.validate_transition(formatted,self.after)
        self.assertEqual(state.validate(previous_publication),state.validate(formatted))
        for metadata_key in ("origin","family","current","additions"):
            self.assertEqual(previous_publication["stateLayout"][metadata_key],
                             published[metadata_key])
        assert_format_history(self,previous_publication,formatted,self.after,ids)
        assert_stable_storage(self,previous_publication,self.after,ids)

    def test_cpp23_recharge_cases_match_and_construction_remains_student_work(self):
        with tempfile.TemporaryDirectory(prefix="recharge-format-") as folder:
            for index,(p,b) in enumerate(POSITIONS):
                identity=self.meta["current"][p]["blocks"][b]
                before,new=self.history[identity],self.after["pages"][p]["blocks"][b]
                cases=new.get("outputCheck",{}).get("cases",[])
                inputs=[x["standardInput"] for x in cases] or [new.get("initialStandardInput","")]
                results=[]
                for label,block in (("old",before),("spaced",new)):
                    src=Path(folder)/f"{index}-{label}.cpp"
                    exe=Path(folder)/f"{index}-{label}"
                    src.write_text(block[slot(block)],encoding="utf-8")
                    compile=subprocess.run(msvc_cpp23_command(exe, src),
                                           capture_output=True,text=True,timeout=90)
                    self.assertEqual(0,compile.returncode,identity+compile.stderr)
                    runs=[]
                    for inp in inputs:
                        run=subprocess.run([str(windows_executable(exe))],input=inp,capture_output=True,text=True,timeout=15)
                        self.assertEqual(0,run.returncode,identity)
                        self.assertEqual("",run.stderr,identity)
                        runs.append((run.returncode,run.stdout,run.stderr))
                    results.append(runs)
                self.assertEqual(results[0],results[1],identity)
                if new["type"]=="output_prediction":
                    self.assertIn(results[1][0][1],new["answers"],identity)
                if (p,b) in STARTERS:
                    self.assertTrue(cases)
                    self.assertNotIn(results[1][0][1],cases[0]["outputs"],identity)
                else:
                    for output,case in zip(results[1],cases):
                        self.assertIn(output[1],case["outputs"],identity)


if __name__=="__main__":
    unittest.main()
