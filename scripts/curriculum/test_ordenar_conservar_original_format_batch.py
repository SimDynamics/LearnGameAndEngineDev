"""U5L10: sorted copies, duplicates and student editors retain historical state."""
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
LESSON=ROOT/"app/src/main/assets/content/es/programming/cpp_fundamentals/colecciones_y_recorridos/lesson_ordenar_y_conservar_el_original.json"
PUBLICATIONS={
    "before": Publication("8d1f9da5f1f3dd19a542c6eaad5ccd876deae545",
        "de55b28d70acc7e685f866e0c88a5d4a93b1e1ba815c05c921ca25dcb2f62666"),
    "formatted": Publication("d7ee91c7f3138dbbe690aa557473f93e04134a6a",
        "dc83d15848931a88ae20584b632660d0240154c99c067c7306ce30714d1d8983"),
}
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
        relative=LESSON.relative_to(ROOT).as_posix()
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
            ident=published["current"][p]["blocks"][b]
            original,current=before_blocks[ident],formatted["pages"][p]["blocks"][b]
            self.assertTrue(require_source_format(original,current),ident)
            field=source_field(original)
            self.assertEqual([line for line in original[field].splitlines() if line.strip()],
                             [line for line in current[field].splitlines() if line.strip()],ident)
            self.assertEqual(original[field].count("std::sort("),
                             current[field].count("std::sort("),ident)
        state.validate_transition(previous_publication,formatted)
        state.validate_transition(formatted,self.current)
        self.assertEqual(state.validate(previous_publication),state.validate(formatted))
        for metadata_key in ("origin","family","current","additions"):
            self.assertEqual(previous_publication["stateLayout"][metadata_key],
                             published[metadata_key])
        assert_format_history(self,previous_publication,formatted,self.current,ids)
        assert_stable_storage(self,previous_publication,self.current,ids)

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
                    built=subprocess.run(msvc_cpp23_command(binary, src),
                                         capture_output=True,text=True,timeout=90)
                    self.assertEqual(0,built.returncode,ident+built.stderr)
                    runs=[]
                    for stdin in inputs:
                        run=subprocess.run([str(windows_executable(binary))],input=stdin,
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
