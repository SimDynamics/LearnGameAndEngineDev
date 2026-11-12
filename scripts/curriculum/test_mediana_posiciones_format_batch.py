"""U5L11 median, even/odd central positions and authentic historical editors."""
from copy import deepcopy
import json
from pathlib import Path
import subprocess
import tempfile
import unittest
import stable_state_layout as state
from code_format_compat import require_source_format

ROOT=Path(__file__).resolve().parents[2]
PATH=ROOT/"app/src/main/assets/content/es/programming/cpp_fundamentals/colecciones_y_recorridos/lesson_mediana_y_posiciones_centrales.json"
POSITIONS=((1,2),(1,4),(2,2),(3,1),(3,3),(4,2),(5,1),(5,2),(6,1),
           (7,2),(7,15),(8,1),(8,4),(8,8),(9,1))
UNSOLVED={(7,2),(7,15)}

def field(bl):
    return "initialCode" if bl["type"]=="cpp_compiler" else ("template" if bl["type"]=="inline_fill_code" else "code")

def runnable(bl):
    source=bl[field(bl)]
    if bl["type"]=="fill_code":
        assert source.count("___")==1
        source=source.replace("___",bl["answers"][0],1)
    if bl["type"]=="inline_fill_code":
        for answer in bl["answers"]:
            assert "[]" in source
            source=source.replace("[]",answer,1)
        assert "[]" not in source
    return source

class MedianFormatBatch(unittest.TestCase):
    def setUp(self):
        self.after=json.loads(PATH.read_text(encoding="utf-8"))
        self.meta=self.after["stateLayout"]
        self.archives={x["id"]:x["block"] for x in self.meta["sourceFormatRevisions"]}

    def test_historial_editores_y_respuestas_originales(self):
        state.validate(self.after)
        self.assertEqual(len(POSITIONS),len(self.archives))
        self.assertEqual([self.meta["current"][p]["blocks"][b] for p,b in POSITIONS],
                         [e["id"] for e in self.meta["editorial"][-len(POSITIONS):]])
        before=deepcopy(self.after)
        before["stateLayout"].pop("sourceFormatRevisions")
        before["stateLayout"]["editorial"]=before["stateLayout"]["editorial"][:-len(POSITIONS)]
        for p,b in POSITIONS:
            id_=self.meta["current"][p]["blocks"][b]
            old,new=self.archives[id_],self.after["pages"][p]["blocks"][b]
            self.assertTrue(require_source_format(old,new),id_)
            slot=field(old)
            self.assertEqual([line for line in old[slot].splitlines() if line.strip()],
                             [line for line in new[slot].splitlines() if line.strip()],id_)
            self.assertEqual(old[slot].count("static_cast<double>"),
                             new[slot].count("static_cast<double>"),id_)
            before["pages"][p]["blocks"][b]=deepcopy(old)
        state.validate(before)
        state.validate_transition(before,self.after)
        self.assertEqual(state.validate(before),state.validate(self.after))
        for key in ("origin","family","current","additions"):
            self.assertEqual(before["stateLayout"][key],self.meta[key])

    def test_cpp23_preceding_and_new_mediana_outputs_are_identical(self):
        with tempfile.TemporaryDirectory(prefix="median-format-") as folder:
            for i,(p,b) in enumerate(POSITIONS):
                id_=self.meta["current"][p]["blocks"][b]
                old,new=self.archives[id_],self.after["pages"][p]["blocks"][b]
                cases=new.get("outputCheck",{}).get("cases",[])
                inputs=[case["standardInput"] for case in cases] or [new.get("initialStandardInput","")]
                variants=[]
                for tag,bl in (("old",old),("new",new)):
                    source=Path(folder)/f"{i}-{tag}.cpp"
                    binary=Path(folder)/f"{i}-{tag}"
                    source.write_text(runnable(bl),encoding="utf-8")
                    built=subprocess.run(["c++","-std=c++23","-O0","-o",str(binary),str(source)],
                                         capture_output=True,text=True,timeout=90)
                    self.assertEqual(0,built.returncode,id_+built.stderr)
                    observations=[]
                    for stdin in inputs:
                        executed=subprocess.run([str(binary)],input=stdin,capture_output=True,text=True,timeout=15)
                        self.assertEqual(0,executed.returncode,id_)
                        self.assertEqual("",executed.stderr,id_)
                        observations.append((executed.returncode,executed.stdout,executed.stderr))
                    variants.append(observations)
                self.assertEqual(variants[0],variants[1],id_)
                if new["type"]=="output_prediction":
                    self.assertIn(variants[1][0][1],new["answers"],id_)
                if (p,b) in UNSOLVED:
                    self.assertNotIn(variants[1][0][1],cases[0]["outputs"],id_)
                else:
                    for result,case in zip(variants[1],cases):
                        self.assertIn(result[1],case["outputs"],id_)

if __name__=="__main__":
    unittest.main()
