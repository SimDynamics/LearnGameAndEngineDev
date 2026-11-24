"""U5L5: index bounds, reverse loop, authentic state and two unsolved programs."""
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
PATH=ROOT/"app/src/main/assets/content/es/programming/cpp_fundamentals/colecciones_y_recorridos/lesson_recorrer_indices_con_seguridad.json"
PUBLICATIONS={
    "before": Publication("811cd93c5819b349d30b0e03f0e0a7187360e3ef",
        "4dc93298677dfcbf9aebc67d49cb7dec2922c4f694a7e42d9a2d7fa2f05a0799"),
    "formatted": Publication("5240c71e10eb70e4765b95a1215a51c296480c01",
        "579e638c4b9e6cd2c7a11aa250908a9e35b1a1be49d42cf9f8345ac1eb48d5d3"),
}
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
        relative=PATH.relative_to(ROOT).as_posix()
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
        self.assertEqual(self.meta["editorial"][:len(published["editorial"])],
                         published["editorial"])
        self.assertEqual(self.meta["sourceFormatRevisions"][:len(archives)],archives)
        before_blocks=all_owners(previous_publication)
        for p,b in POSITIONS:
            identity=published["current"][p]["blocks"][b]
            before,after=before_blocks[identity],formatted["pages"][p]["blocks"][b]
            self.assertTrue(require_source_format(before,after),identity)
            slot=field(before)
            self.assertEqual([line for line in before[slot].splitlines() if line.strip()],
                             [line for line in after[slot].splitlines() if line.strip()],identity)
        state.validate_transition(previous_publication,formatted)
        state.validate_transition(formatted,self.doc)
        self.assertEqual(state.validate(previous_publication),state.validate(formatted))
        for metadata_key in ("origin","family","current","additions"):
            self.assertEqual(previous_publication["stateLayout"][metadata_key],
                             published[metadata_key])
        assert_format_history(self,previous_publication,formatted,self.doc,ids)
        assert_stable_storage(self,previous_publication,self.doc,ids)

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
                    built=subprocess.run(msvc_cpp23_command(executable, src),
                                         capture_output=True,text=True,timeout=90)
                    self.assertEqual(0,built.returncode,identity+built.stderr)
                    runs=[]
                    for stdin in inputs:
                        run=subprocess.run([str(windows_executable(executable))],input=stdin,
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
