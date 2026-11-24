"""U5L9: guarded mean on empty vectors, floating point and unchanged tasks."""
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
PATH=ROOT/"app/src/main/assets/content/es/programming/cpp_fundamentals/colecciones_y_recorridos/lesson_media_y_cantidad_de_datos.json"
PUBLICATIONS={
    "before": Publication("7365873ddb2aecd651a5ddbaab3d24de53315dbb",
        "32411f82398a7a878f62e2e6f372944e325be57aece006b78949ab064c3ebe8c"),
    "formatted": Publication("1db7d6505c1d0fa8b299e460ef63b3efa813e3be",
        "31cc6d19ddd1f3b38fcb729d99e47c30afea1f8e7f9da6ca35dde3ff50bb25c7"),
}
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
        relative=PATH.relative_to(ROOT).as_posix()
        previous_publication=load_publication(ROOT,relative,PUBLICATIONS["before"])
        formatted=load_publication(ROOT,relative,PUBLICATIONS["formatted"])
        for document in (previous_publication,formatted,self.current):
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
            old,new=before_blocks[identity],formatted["pages"][p]["blocks"][b]
            self.assertTrue(require_source_format(old,new),identity)
            source_slot=slot(old)
            self.assertEqual([l for l in old[source_slot].splitlines() if l.strip()],
                             [l for l in new[source_slot].splitlines() if l.strip()],identity)
            self.assertEqual(old[source_slot].count("} else {"),
                             new[source_slot].count("} else {"),identity)
        state.validate_transition(previous_publication,formatted)
        state.validate_transition(formatted,self.current)
        self.assertEqual(state.validate(previous_publication),state.validate(formatted))
        for metadata_key in ("origin","family","current","additions"):
            self.assertEqual(previous_publication["stateLayout"][metadata_key],
                             published[metadata_key])
        assert_format_history(self,previous_publication,formatted,self.current,ids)
        assert_stable_storage(self,previous_publication,self.current,ids)

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
                    built=subprocess.run(msvc_cpp23_command(executable, source),
                                         capture_output=True,text=True,timeout=90)
                    self.assertEqual(0,built.returncode,identity+built.stderr)
                    observations=[]
                    for stdin in inputs:
                        run=subprocess.run([str(windows_executable(executable))],input=stdin,capture_output=True,
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
