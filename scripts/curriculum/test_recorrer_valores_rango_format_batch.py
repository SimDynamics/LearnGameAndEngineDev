"""U5L3: range loops by value, no accidental mutation, preserved C++23 state."""
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
PATH=ROOT/"app/src/main/assets/content/es/programming/cpp_fundamentals/colecciones_y_recorridos/lesson_recorrer_valores_por_rango.json"
PUBLICATIONS={
    "before": Publication("19d4f7a9ac4d775b7641ef5cccdf62818bb79447",
        "6812395ca5851431c81f708e3a8035a2f2b7217de5421a07ffcac2fcf62e75dc"),
    "formatted": Publication("986d6d0c4e49abc591c9291ed1e5b696724eed7f",
        "33ba9928d3a2a0233ba6f67348ade213a3f1ec39de984726df0770d90de338c9"),
}
POSITIONS=((0,2),(1,1),(1,3),(2,1),(2,2),(3,2),(4,1),
           (4,2),(5,1),(5,3),(6,2),(7,1),(7,2),(7,7))
INCOMPLETE={(6,2)}


def field(block):
    if block["type"]=="cpp_compiler": return "initialCode"
    if block["type"]=="inline_fill_code": return "template"
    return "code"


def runnable(block):
    code=block[field(block)]
    if block["type"]=="fill_code":
        assert code.count("___")==1
        code=code.replace("___",block["answers"][0])
    if block["type"]=="inline_fill_code":
        for answer in block["answers"]:
            assert "[]" in code
            code=code.replace("[]",answer,1)
    return code


class RangeLoopFormatBatch(unittest.TestCase):
    def setUp(self):
        self.doc=json.loads(PATH.read_text(encoding="utf-8"))
        self.meta=self.doc["stateLayout"]
        self.archive={e["id"]:e["block"] for e in self.meta["sourceFormatRevisions"]}

    def test_source_only_changes_do_not_affect_older_answers_or_drafts(self):
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

    def test_cpp23_original_and_new_outputs_match_all_optional_inputs(self):
        with tempfile.TemporaryDirectory(prefix="u5l3-range-format-") as folder:
            for idx,(p,b) in enumerate(POSITIONS):
                identity=self.meta["current"][p]["blocks"][b]
                before,after=self.archive[identity],self.doc["pages"][p]["blocks"][b]
                cases=after.get("outputCheck",{}).get("cases",[])
                inputs=[c["standardInput"] for c in cases] or [after.get("initialStandardInput","")]
                observations=[]
                for name,block in (("old",before),("new",after)):
                    source=Path(folder)/f"{idx}-{name}.cpp"
                    binary=Path(folder)/f"{idx}-{name}"
                    source.write_text(runnable(block),encoding="utf-8")
                    compiled=subprocess.run(msvc_cpp23_command(binary, source),
                                            capture_output=True,text=True,timeout=90)
                    self.assertEqual(0,compiled.returncode,identity+compiled.stderr)
                    runs=[]
                    for data in inputs:
                        executed=subprocess.run([str(windows_executable(binary))],input=data,
                                                 capture_output=True,text=True,timeout=15)
                        self.assertEqual(0,executed.returncode,identity)
                        self.assertEqual("",executed.stderr,identity)
                        runs.append((executed.returncode,executed.stdout,executed.stderr))
                    observations.append(runs)
                self.assertEqual(observations[0],observations[1],identity)
                if after["type"]=="output_prediction":
                    self.assertIn(observations[1][0][1],after["answers"],identity)
                if (p,b) in INCOMPLETE:
                    self.assertNotIn(observations[1][0][1],cases[0]["outputs"],identity)
                else:
                    for result,case in zip(observations[1],cases):
                        self.assertIn(result[1],case["outputs"],identity)


if __name__=="__main__":
    unittest.main()
