"""Regresión U4L6: switch, case y fallthrough, sin modificar soluciones."""
from copy import deepcopy
import json
from pathlib import Path
import subprocess
import tempfile
import unittest

import stable_state_layout as state
from code_format_compat import require_source_format

ROOT = Path(__file__).resolve().parents[2]
PATH = ROOT / "app/src/main/assets/content/es/programming/cpp_fundamentals/repeticion_y_terminacion/lesson_seleccion_discreta_con_switch.json"
POSITIONS = ((0,2),(0,4),(1,1),(1,2),(2,2),(2,3),(2,4),(3,2),
             (4,2),(4,4),(5,2),(5,6),(6,3),(6,5),(7,1),(7,3),
             (8,4),(9,2),(9,6),(9,10))
UNSOLVED = {(4,4),(8,4)}


def code_field(block):
    return "initialCode" if block["type"]=="cpp_compiler" else "code"


def source(block):
    text=block[code_field(block)]
    if block["type"]=="fill_code":
        assert text.count("___")==1
        text=text.replace("___",block["answers"][0])
    return text


class SwitchHistoryFormatBatch(unittest.TestCase):
    def setUp(self):
        self.doc=json.loads(PATH.read_text(encoding="utf-8"))
        self.meta=self.doc["stateLayout"]
        self.old={e["id"]:e["block"] for e in self.meta["sourceFormatRevisions"]}

    def test_originals_preserve_switch_semantics_answers_and_state(self):
        state.validate(self.doc)
        self.assertEqual(len(POSITIONS),len(self.old))
        ids=[self.meta["current"][p]["blocks"][b] for p,b in POSITIONS]
        self.assertEqual(ids,[e["id"] for e in self.meta["editorial"][-len(POSITIONS):]])
        prior=deepcopy(self.doc)
        prior["stateLayout"].pop("sourceFormatRevisions")
        prior["stateLayout"]["editorial"]=prior["stateLayout"]["editorial"][:-len(POSITIONS)]
        for p,b in POSITIONS:
            id_=self.meta["current"][p]["blocks"][b]
            old,new=self.old[id_],self.doc["pages"][p]["blocks"][b]
            self.assertTrue(require_source_format(old,new),id_)
            field=code_field(old)
            self.assertEqual([l for l in old[field].splitlines() if l.strip()],
                             [l for l in new[field].splitlines() if l.strip()],id_)
            self.assertEqual(old[field].count("[[fallthrough]]"),
                             new[field].count("[[fallthrough]]"),id_)
            prior["pages"][p]["blocks"][b]=deepcopy(old)
        state.validate(prior)
        state.validate_transition(prior,self.doc)
        self.assertEqual(state.validate(prior),state.validate(self.doc))
        for key in ("origin","family","current","additions"):
            self.assertEqual(prior["stateLayout"][key],self.meta[key])

    def test_cpp23_syntax_runtime_and_unfinished_cases(self):
        with tempfile.TemporaryDirectory(prefix="switch-format-") as directory:
            for i,(p,b) in enumerate(POSITIONS):
                identity=self.meta["current"][p]["blocks"][b]
                old,new=self.old[identity],self.doc["pages"][p]["blocks"][b]
                cases=new.get("outputCheck",{}).get("cases",[])
                inputs=[case["standardInput"] for case in cases] or [new.get("initialStandardInput","")]
                variants=[]
                for tag,bl in (("old",old),("spaced",new)):
                    path=Path(directory)/f"{i}-{tag}.cpp"
                    exe=Path(directory)/f"{i}-{tag}"
                    path.write_text(source(bl),encoding="utf-8")
                    built=subprocess.run(["c++","-std=c++23","-O0","-o",str(exe),str(path)],
                                         capture_output=True,text=True,timeout=90)
                    self.assertEqual(0,built.returncode,identity+built.stderr)
                    runs=[]
                    for inp in inputs:
                        p0=subprocess.run([str(exe)],input=inp,
                                          capture_output=True,text=True,timeout=15)
                        self.assertEqual(0,p0.returncode,identity)
                        self.assertEqual("",p0.stderr,identity)
                        runs.append((p0.returncode,p0.stdout,p0.stderr))
                    variants.append(runs)
                self.assertEqual(variants[0],variants[1],identity)
                if new["type"]=="output_prediction":
                    self.assertIn(variants[1][0][1],new["answers"],identity)
                if (p,b) in UNSOLVED:
                    self.assertNotIn(variants[1][0][1],cases[0]["outputs"],identity)
                else:
                    for result,case in zip(variants[1],cases):
                        self.assertIn(result[1],case["outputs"],identity)


if __name__=="__main__":
    unittest.main()
