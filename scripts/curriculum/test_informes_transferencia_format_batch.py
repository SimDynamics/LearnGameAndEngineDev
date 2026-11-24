"""U5L12: numerical reports, copy versus reference and historical contracts."""
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
LESSON=ROOT/"app/src/main/assets/content/es/programming/cpp_fundamentals/colecciones_y_recorridos/lesson_informes_y_transferencia_de_colecciones.json"
PUBLICATIONS={
    "before": Publication("b19289c6cd66a97e308e86b95c3e710a2a8e6436",
        "64af502bd98f4a53f491a27fcf7d297f7234c7ea128ba7c1942e606f671cd4e7"),
    "formatted": Publication("0e66f3dd98960fe1dcd09978a1037488d3ff9f06",
        "800c53bc8fa672395e3c1768e0ecdad574848ed4bdb30e28f5554a9e30c1c236"),
}
POSITIONS=((2,2),(2,14),(3,1),(3,6),(4,1),(4,2),(6,2),(6,15),(7,1),(7,5),(7,9),
           (8,1),(9,7),(9,19),(9,22),(9,25),(9,28))
UNSOLVED={(2,2),(2,14),(6,2),(6,15),(9,7),(9,19),(9,25)}

def field(block):
    return "initialCode" if block["type"]=="cpp_compiler" else "code"

def runnable(block):
    src=block[field(block)]
    if block["type"]=="fill_code":
        assert src.count("___")==1
        src=src.replace("___",block["answers"][0],1)
    return src

class ReportsTransferFormat(unittest.TestCase):
    def setUp(self):
        self.doc=json.loads(LESSON.read_text(encoding="utf-8"))
        self.state=self.doc["stateLayout"]
        self.old={v["id"]:v["block"] for v in self.state["sourceFormatRevisions"]}

    def test_original_identity_responses_and_editor_state_preserved(self):
        relative=LESSON.relative_to(ROOT).as_posix()
        previous_publication=load_publication(ROOT,relative,PUBLICATIONS["before"])
        formatted=load_publication(ROOT,relative,PUBLICATIONS["formatted"])
        for document in (previous_publication,formatted,self.doc):
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
        self.assertEqual(self.state["editorial"][:len(published["editorial"])],
                         published["editorial"])
        self.assertEqual(self.state["sourceFormatRevisions"][:len(archives)],archives)
        before_blocks=all_owners(previous_publication)
        for p,b in POSITIONS:
            identity=published["current"][p]["blocks"][b]
            before,after=before_blocks[identity],formatted["pages"][p]["blocks"][b]
            self.assertTrue(require_source_format(before,after),identity)
            key=field(before)
            self.assertEqual([line for line in before[key].splitlines() if line.strip()],
                             [line for line in after[key].splitlines() if line.strip()],identity)
        state.validate_transition(previous_publication,formatted)
        state.validate_transition(formatted,self.doc)
        self.assertEqual(state.validate(previous_publication),state.validate(formatted))
        for metadata_key in ("origin","family","current","additions"):
            self.assertEqual(previous_publication["stateLayout"][metadata_key],
                             published[metadata_key])
        assert_format_history(self,previous_publication,formatted,self.doc,ids)
        assert_stable_storage(self,previous_publication,self.doc,ids)

    def test_cpp23_original_vs_formatted_all_declared_cases(self):
        with tempfile.TemporaryDirectory(prefix="reports-format-") as directory:
            for i,(p,b) in enumerate(POSITIONS):
                ident=self.state["current"][p]["blocks"][b]
                old,new=self.old[ident],self.doc["pages"][p]["blocks"][b]
                cases=new.get("outputCheck",{}).get("cases",[])
                inputs=[case["standardInput"] for case in cases] or [new.get("initialStandardInput","")]
                records=[]
                for tag,block in (("old",old),("new",new)):
                    source=Path(directory)/f"{i}-{tag}.cpp"
                    executable=Path(directory)/f"{i}-{tag}"
                    source.write_text(runnable(block),encoding="utf-8")
                    build=subprocess.run(msvc_cpp23_command(executable, source),
                                         capture_output=True,text=True,timeout=90)
                    self.assertEqual(0,build.returncode,ident+build.stderr)
                    runs=[]
                    for stdin in inputs:
                        r=subprocess.run([str(windows_executable(executable))],input=stdin,capture_output=True,text=True,timeout=15)
                        self.assertEqual(0,r.returncode,ident)
                        self.assertEqual("",r.stderr,ident)
                        runs.append((r.returncode,r.stdout,r.stderr))
                    records.append(runs)
                self.assertEqual(records[0],records[1],ident)
                if new["type"]=="output_prediction":
                    self.assertIn(records[1][0][1],new["answers"],ident)
                if (p,b) in UNSOLVED:
                    self.assertTrue(cases,ident)
                    self.assertNotIn(records[1][0][1],cases[0]["outputs"],ident)
                else:
                    for result,case in zip(records[1],cases):
                        self.assertIn(result[1],case["outputs"],ident)

if __name__=="__main__":
    unittest.main()
