"""U5L2: valid indices, empty vector, guarded at() and historical state."""
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
PATH=ROOT/"app/src/main/assets/content/es/programming/cpp_fundamentals/colecciones_y_recorridos/lesson_indices_y_limites_de_acceso.json"
PUBLICATIONS={
    "before": Publication("c20db82ddb28f5040e63707c6eec54044f8fc38c",
        "67d2581bfc49678c0f1334a4b225f1e19eccb6693caf0efbee7ba5cf34b7fc23"),
    "formatted": Publication("19d4f7a9ac4d775b7641ef5cccdf62818bb79447",
        "9ee9f6df206ba1551775cb02887ed49c884c1d78db2d54c57ea967b76ebf6b51"),
}
POSITIONS=((0,2),(0,4),(2,2),(2,3),(3,2),(4,1),(4,2),(4,3),
           (5,2),(6,2),(7,1),(7,2),(7,7))
INCOMPLETE={(6,2)}


def field(block):
    return "initialCode" if block["type"]=="cpp_compiler" else "code"


def runnable(block):
    code=block[field(block)]
    if block["type"]=="fill_code":
        assert code.count("___")==1
        code=code.replace("___",block["answers"][0])
    return code


class IndicesLimitesFormat(unittest.TestCase):
    def setUp(self):
        self.doc=json.loads(PATH.read_text(encoding="utf-8"))
        self.meta=self.doc["stateLayout"]
        self.previous={item["id"]:item["block"] for item in self.meta["sourceFormatRevisions"]}

    def test_history_and_protected_conditions_preserved(self):
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
            self.assertEqual(before[key].count("} else {"),after[key].count("} else {"),identity)
            self.assertEqual(before[key].count(".at("),after[key].count(".at("),identity)
        state.validate_transition(previous_publication,formatted)
        state.validate_transition(formatted,self.doc)
        self.assertEqual(state.validate(previous_publication),state.validate(formatted))
        for metadata_key in ("origin","family","current","additions"):
            self.assertEqual(previous_publication["stateLayout"][metadata_key],
                             published[metadata_key])
        assert_format_history(self,previous_publication,formatted,self.doc,ids)
        assert_stable_storage(self,previous_publication,self.doc,ids)

    def test_all_optional_cpp23_inputs_and_outcomes_stay_identical(self):
        with tempfile.TemporaryDirectory(prefix="indices-format-") as folder:
            for index,(p,b) in enumerate(POSITIONS):
                identity=self.meta["current"][p]["blocks"][b]
                before,after=self.previous[identity],self.doc["pages"][p]["blocks"][b]
                cases=after.get("outputCheck",{}).get("cases",[])
                inputs=[c["standardInput"] for c in cases] or [after.get("initialStandardInput","")]
                observations=[]
                for label,block in (("old",before),("new",after)):
                    path=Path(folder)/f"{index}-{label}.cpp"
                    binary=Path(folder)/f"{index}-{label}"
                    path.write_text(runnable(block),encoding="utf-8")
                    built=subprocess.run(msvc_cpp23_command(binary, path),
                                         capture_output=True,text=True,timeout=90)
                    self.assertEqual(0,built.returncode,identity+built.stderr)
                    runs=[]
                    for stdin in inputs:
                        run=subprocess.run([str(windows_executable(binary))],input=stdin,capture_output=True,text=True,timeout=15)
                        self.assertEqual(0,run.returncode,identity)
                        self.assertEqual("",run.stderr,identity)
                        runs.append((run.returncode,run.stdout,run.stderr))
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
