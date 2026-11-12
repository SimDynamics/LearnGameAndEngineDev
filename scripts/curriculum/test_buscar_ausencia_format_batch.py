"""U5L7: find first match, model absence, preserve history and unfinished editors."""
from copy import deepcopy
import json
from pathlib import Path
import subprocess
import tempfile
import unittest

import stable_state_layout as state
from code_format_compat import require_source_format

ROOT=Path(__file__).resolve().parents[2]
PATH=ROOT/"app/src/main/assets/content/es/programming/cpp_fundamentals/colecciones_y_recorridos/lesson_buscar_y_representar_ausencia.json"
POSITIONS=((0,2),(1,1),(1,2),(2,2),(3,1),(3,3),(4,1),
           (5,1),(6,2),(7,1),(7,2),(7,7),(7,10),(7,13))
INCOMPLETE={(6,2),(7,10)}


def field(block):
    return "initialCode" if block["type"]=="cpp_compiler" else "code"


def runnable(block):
    src=block[field(block)]
    if block["type"]=="fill_code":
        assert src.count("___")==1
        src=src.replace("___",block["answers"][0])
    return src


class FindAbsentFormatBatch(unittest.TestCase):
    def setUp(self):
        self.doc=json.loads(PATH.read_text(encoding="utf-8"))
        self.meta=self.doc["stateLayout"]
        self.archive={x["id"]:x["block"] for x in self.meta["sourceFormatRevisions"]}

    def test_lexical_edits_preserve_history_and_absence_guards(self):
        state.validate(self.doc)
        self.assertEqual(len(POSITIONS),len(self.archive))
        self.assertEqual([self.meta["current"][p]["blocks"][b] for p,b in POSITIONS],
                         [x["id"] for x in self.meta["editorial"][-len(POSITIONS):]])
        prior=deepcopy(self.doc)
        prior["stateLayout"].pop("sourceFormatRevisions")
        prior["stateLayout"]["editorial"]=prior["stateLayout"]["editorial"][:-len(POSITIONS)]
        for p,b in POSITIONS:
            identity=self.meta["current"][p]["blocks"][b]
            before,after=self.archive[identity],self.doc["pages"][p]["blocks"][b]
            self.assertTrue(require_source_format(before,after),identity)
            slot=field(before)
            self.assertEqual([l for l in before[slot].splitlines() if l.strip()],
                             [l for l in after[slot].splitlines() if l.strip()],identity)
            self.assertEqual(before[slot].count("} else {"),after[slot].count("} else {"),identity)
            self.assertEqual(before[slot].count("posicion == datos.size()"),
                             after[slot].count("posicion == datos.size()"),identity)
            prior["pages"][p]["blocks"][b]=deepcopy(before)
        state.validate(prior)
        state.validate_transition(prior,self.doc)
        self.assertEqual(state.validate(prior),state.validate(self.doc))
        for key in ("origin","family","current","additions"):
            self.assertEqual(prior["stateLayout"][key],self.meta[key])

    def test_cpp23_original_and_formatted_outputs_match_expected_oracle(self):
        with tempfile.TemporaryDirectory(prefix="find-absence-format-") as folder:
            for index,(p,b) in enumerate(POSITIONS):
                identity=self.meta["current"][p]["blocks"][b]
                before,after=self.archive[identity],self.doc["pages"][p]["blocks"][b]
                cases=after.get("outputCheck",{}).get("cases",[])
                inputs=[c["standardInput"] for c in cases] or [after.get("initialStandardInput","")]
                records=[]
                for version,block in (("original",before),("formatted",after)):
                    source=Path(folder)/f"{index}-{version}.cpp"
                    exe=Path(folder)/f"{index}-{version}"
                    source.write_text(runnable(block),encoding="utf-8")
                    build=subprocess.run(["c++","-std=c++23","-O0","-o",str(exe),str(source)],
                                         capture_output=True,text=True,timeout=90)
                    self.assertEqual(0,build.returncode,identity+build.stderr)
                    runs=[]
                    for stdin in inputs:
                        result=subprocess.run([str(exe)],input=stdin,capture_output=True,
                                              text=True,timeout=15)
                        self.assertEqual(0,result.returncode,identity)
                        self.assertEqual("",result.stderr,identity)
                        runs.append((result.returncode,result.stdout,result.stderr))
                    records.append(runs)
                self.assertEqual(records[0],records[1],identity)
                if after["type"]=="output_prediction":
                    self.assertIn(records[1][0][1],after["answers"],identity)
                if (p,b) in INCOMPLETE:
                    self.assertNotIn(records[1][0][1],cases[0]["outputs"],identity)
                else:
                    for observation,case in zip(records[1],cases):
                        self.assertIn(observation[1],case["outputs"],identity)


if __name__=="__main__":
    unittest.main()
