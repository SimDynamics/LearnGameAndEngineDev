"""U5L3: range loops by value, no accidental mutation, preserved C++23 state."""
from copy import deepcopy
import json
from pathlib import Path
import subprocess
import tempfile
import unittest

import stable_state_layout as state
from code_format_compat import require_source_format

ROOT=Path(__file__).resolve().parents[2]
PATH=ROOT/"app/src/main/assets/content/es/programming/cpp_fundamentals/colecciones_y_recorridos/lesson_recorrer_valores_por_rango.json"
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
        state.validate(self.doc)
        self.assertEqual(len(POSITIONS),len(self.archive))
        identities=[self.meta["current"][p]["blocks"][b] for p,b in POSITIONS]
        self.assertEqual(identities,[e["id"] for e in self.meta["editorial"][-len(POSITIONS):]])
        older=deepcopy(self.doc)
        older["stateLayout"].pop("sourceFormatRevisions")
        older["stateLayout"]["editorial"]=older["stateLayout"]["editorial"][:-len(POSITIONS)]
        for p,b in POSITIONS:
            identity=self.meta["current"][p]["blocks"][b]
            before,after=self.archive[identity],self.doc["pages"][p]["blocks"][b]
            self.assertTrue(require_source_format(before,after),identity)
            key=field(before)
            self.assertEqual([line for line in before[key].splitlines() if line.strip()],
                             [line for line in after[key].splitlines() if line.strip()],identity)
            older["pages"][p]["blocks"][b]=deepcopy(before)
        state.validate(older)
        state.validate_transition(older,self.doc)
        self.assertEqual(state.validate(older),state.validate(self.doc))
        for k in ("origin","family","current","additions"):
            self.assertEqual(older["stateLayout"][k],self.meta[k])

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
                    compiled=subprocess.run(["c++","-std=c++23","-O0","-o",str(binary),str(source)],
                                            capture_output=True,text=True,timeout=90)
                    self.assertEqual(0,compiled.returncode,identity+compiled.stderr)
                    runs=[]
                    for data in inputs:
                        executed=subprocess.run([str(binary)],input=data,
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
