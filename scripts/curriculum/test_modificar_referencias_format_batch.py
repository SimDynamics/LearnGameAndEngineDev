"""U5L4: copies versus references and historical task state preserved."""
from copy import deepcopy
import json
from pathlib import Path
import subprocess
import tempfile
import unittest

import stable_state_layout as state
from code_format_compat import require_source_format

ROOT=Path(__file__).resolve().parents[2]
PATH=ROOT/"app/src/main/assets/content/es/programming/cpp_fundamentals/colecciones_y_recorridos/lesson_modificar_con_referencias.json"
POSITIONS=((0,2),(1,1),(1,3),(2,2),(2,3),(3,2),(4,2),
           (5,1),(6,2),(7,1),(7,2),(7,7),(7,10))
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
        assert src.count("[]")==1
        src=src.replace("[]",block["answers"][0])
    return src


class ReferenceHistoryFormat(unittest.TestCase):
    def setUp(self):
        self.current=json.loads(PATH.read_text(encoding="utf-8"))
        self.meta=self.current["stateLayout"]
        self.archive={row["id"]:row["block"] for row in self.meta["sourceFormatRevisions"]}

    def test_format_only_changes_preserve_reference_semantics_and_answers(self):
        state.validate(self.current)
        self.assertEqual(len(POSITIONS),len(self.archive))
        ids=[self.meta["current"][p]["blocks"][b] for p,b in POSITIONS]
        self.assertEqual(ids,[e["id"] for e in self.meta["editorial"][-len(POSITIONS):]])
        original=deepcopy(self.current)
        original["stateLayout"].pop("sourceFormatRevisions")
        original["stateLayout"]["editorial"]=original["stateLayout"]["editorial"][:-len(POSITIONS)]
        for p,b in POSITIONS:
            identity=self.meta["current"][p]["blocks"][b]
            before,after=self.archive[identity],self.current["pages"][p]["blocks"][b]
            self.assertTrue(require_source_format(before,after),identity)
            slot=field(before)
            self.assertEqual([ln for ln in before[slot].splitlines() if ln.strip()],
                             [ln for ln in after[slot].splitlines() if ln.strip()],identity)
            self.assertEqual(before[slot].count("auto&"),after[slot].count("auto&"),identity)
            original["pages"][p]["blocks"][b]=deepcopy(before)
        state.validate(original)
        state.validate_transition(original,self.current)
        self.assertEqual(state.validate(original),state.validate(self.current))
        for key in ("origin","family","current","additions"):
            self.assertEqual(original["stateLayout"][key],self.meta[key])

    def test_cpp23_preceding_and_current_sources_match_all_cases(self):
        with tempfile.TemporaryDirectory(prefix="references-format-") as folder:
            for index,(p,b) in enumerate(POSITIONS):
                identity=self.meta["current"][p]["blocks"][b]
                before,after=self.archive[identity],self.current["pages"][p]["blocks"][b]
                cases=after.get("outputCheck",{}).get("cases",[])
                inputs=[item["standardInput"] for item in cases] or [after.get("initialStandardInput","")]
                observations=[]
                for version,block in (("before",before),("after",after)):
                    source=Path(folder)/f"{index}-{version}.cpp"
                    binary=Path(folder)/f"{index}-{version}"
                    source.write_text(runnable(block),encoding="utf-8")
                    compiled=subprocess.run(["c++","-std=c++23","-O0","-o",str(binary),str(source)],
                                            capture_output=True,text=True,timeout=90)
                    self.assertEqual(0,compiled.returncode,identity+compiled.stderr)
                    runs=[]
                    for stdin in inputs:
                        run=subprocess.run([str(binary)],input=stdin,capture_output=True,text=True,timeout=15)
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
