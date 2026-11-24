"""U5L6: accumulation, selections and archived order answers remain intact."""
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
PATH=ROOT/"app/src/main/assets/content/es/programming/cpp_fundamentals/colecciones_y_recorridos/lesson_acumular_contar_y_seleccionar.json"
PUBLICATIONS={
    "before": Publication("3060547d2d96c6006486a05dd701b5f6e03299ff",
        "56c445644d4e6150a9e15c47f657679b12656e405a3adf0c4bf1e5a5b2f6c512"),
    "formatted": Publication("c0f6f8fdf4ccf43704deeb9ce6f6388e4cb0ffc7",
        "e3200e0365662455ab6c025177905fad370b884329c3c245f8453c121081b755"),
}
POSITIONS=((0,2),(1,1),(1,2),(2,1),(2,2),(3,2),(4,2),(4,3),
           (5,2),(6,2),(7,1),(7,2),(7,7))
ORDER=(5,4)
INCOMPLETE={(6,2)}


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


class AccumulateAndSelectFormat(unittest.TestCase):
    def setUp(self):
        self.doc=json.loads(PATH.read_text(encoding="utf-8"))
        self.meta=self.doc["stateLayout"]
        self.originals={x["id"]:x["block"] for x in self.meta["sourceFormatRevisions"]}

    def test_historical_responses_drafts_and_order_remain_valid(self):
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
        self.assertEqual(1,len(published["orderRefinements"]))
        order=formatted["pages"][ORDER[0]]["blocks"][ORDER[1]]
        self.assertEqual("order_code",order["type"])
        self.assertEqual(11,len(order["fragments"]))
        self.assertTrue(all("\n" not in part and "\r" not in part
                            for part in order["fragments"]))
        before_blocks=all_owners(previous_publication)
        for p,b in POSITIONS:
            identity=published["current"][p]["blocks"][b]
            original,updated=before_blocks[identity],formatted["pages"][p]["blocks"][b]
            self.assertTrue(require_source_format(original,updated),identity)
            key=field(original)
            self.assertEqual([line for line in original[key].splitlines() if line.strip()],
                             [line for line in updated[key].splitlines() if line.strip()],identity)
        self.assertEqual(previous_publication["stateLayout"]["orderRefinements"],
                         published["orderRefinements"])
        self.assertEqual(previous_publication["orderVisualSeparators"],
                         formatted["orderVisualSeparators"])
        self.assertEqual(previous_publication["pages"][ORDER[0]]["blocks"][ORDER[1]],order)
        state.validate_transition(previous_publication,formatted)
        state.validate_transition(formatted,self.doc)
        self.assertEqual(state.validate(previous_publication),state.validate(formatted))
        for metadata_key in ("origin","family","current","additions"):
            self.assertEqual(previous_publication["stateLayout"][metadata_key],
                             published[metadata_key])
        assert_format_history(self,previous_publication,formatted,self.doc,ids)
        assert_stable_storage(self,previous_publication,self.doc,ids)

    def test_old_and_new_cpp23_match_all_declared_inputs(self):
        with tempfile.TemporaryDirectory(prefix="u5l6-accumulate-") as folder:
            for index,(p,b) in enumerate(POSITIONS):
                identity=self.meta["current"][p]["blocks"][b]
                before,after=self.originals[identity],self.doc["pages"][p]["blocks"][b]
                cases=after.get("outputCheck",{}).get("cases",[])
                inputs=[case["standardInput"] for case in cases] or [after.get("initialStandardInput","")]
                variants=[]
                for label,block in (("old",before),("formatted",after)):
                    source=Path(folder)/f"{index}-{label}.cpp"
                    exe=Path(folder)/f"{index}-{label}"
                    source.write_text(runnable(block),encoding="utf-8")
                    build=subprocess.run(msvc_cpp23_command(exe, source),
                                         capture_output=True,text=True,timeout=90)
                    self.assertEqual(0,build.returncode,identity+build.stderr)
                    runs=[]
                    for stdin in inputs:
                        executed=subprocess.run([str(windows_executable(exe))],input=stdin,capture_output=True,text=True,timeout=15)
                        self.assertEqual(0,executed.returncode,identity)
                        self.assertEqual("",executed.stderr,identity)
                        runs.append((executed.returncode,executed.stdout,executed.stderr))
                    variants.append(runs)
                self.assertEqual(variants[0],variants[1],identity)
                if after["type"]=="output_prediction":
                    self.assertIn(variants[1][0][1],after["answers"],identity)
                if (p,b) in INCOMPLETE:
                    self.assertTrue(cases,identity)
                    self.assertNotIn(variants[1][0][1],cases[0]["outputs"],identity)
                else:
                    for result,case in zip(variants[1],cases):
                        self.assertIn(result[1],case["outputs"],identity)


if __name__=="__main__":
    unittest.main()
