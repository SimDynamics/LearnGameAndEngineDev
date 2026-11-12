"""U5L6: accumulation, selections and archived order answers remain intact."""
from copy import deepcopy
import json
from pathlib import Path
import subprocess
import tempfile
import unittest

import stable_state_layout as state
from code_format_compat import require_source_format

ROOT=Path(__file__).resolve().parents[2]
PATH=ROOT/"app/src/main/assets/content/es/programming/cpp_fundamentals/colecciones_y_recorridos/lesson_acumular_contar_y_seleccionar.json"
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
        state.validate(self.doc)
        self.assertEqual(len(POSITIONS),len(self.originals))
        self.assertEqual(1,len(self.meta["orderRefinements"]))
        order=self.doc["pages"][ORDER[0]]["blocks"][ORDER[1]]
        self.assertEqual("order_code",order["type"])
        self.assertEqual(11,len(order["fragments"]))
        self.assertTrue(all("\n" not in part and "\r" not in part
                            for part in order["fragments"]))
        self.assertEqual([self.meta["current"][p]["blocks"][b] for p,b in POSITIONS],
                         [e["id"] for e in self.meta["editorial"][-len(POSITIONS):]])
        before=deepcopy(self.doc)
        before["stateLayout"].pop("sourceFormatRevisions")
        before["stateLayout"]["editorial"]=before["stateLayout"]["editorial"][:-len(POSITIONS)]
        for p,b in POSITIONS:
            identity=self.meta["current"][p]["blocks"][b]
            original,updated=self.originals[identity],self.doc["pages"][p]["blocks"][b]
            self.assertTrue(require_source_format(original,updated),identity)
            key=field(original)
            self.assertEqual([line for line in original[key].splitlines() if line.strip()],
                             [line for line in updated[key].splitlines() if line.strip()],identity)
            before["pages"][p]["blocks"][b]=deepcopy(original)
        self.assertEqual(before["stateLayout"]["orderRefinements"],self.meta["orderRefinements"])
        self.assertEqual(before["orderVisualSeparators"],self.doc["orderVisualSeparators"])
        self.assertEqual(before["pages"][ORDER[0]]["blocks"][ORDER[1]],order)
        state.validate(before)
        state.validate_transition(before,self.doc)
        self.assertEqual(state.validate(before),state.validate(self.doc))
        for k in ("origin","family","current","additions"):
            self.assertEqual(before["stateLayout"][k],self.meta[k])

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
                    build=subprocess.run(["c++","-std=c++23","-O0","-o",str(exe),str(source)],
                                         capture_output=True,text=True,timeout=90)
                    self.assertEqual(0,build.returncode,identity+build.stderr)
                    runs=[]
                    for stdin in inputs:
                        executed=subprocess.run([str(exe)],input=stdin,capture_output=True,text=True,timeout=15)
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
