"""U4L3: acumuladores y puzle de orden autentico, sin reescribir respuestas."""
from copy import deepcopy
import json
from pathlib import Path
import subprocess
import tempfile
import unittest

import stable_state_layout as state
from code_format_compat import require_source_format

ROOT=Path(__file__).resolve().parents[2]
PATH=ROOT/"app/src/main/assets/content/es/programming/cpp_fundamentals/repeticion_y_terminacion/lesson_acumuladores_contadores_y_trazas.json"
POSITIONS=((0,3),(0,5),(1,2),(1,3),(2,1),(2,2),(3,3),(3,4),(3,5),(4,3),(5,2),(5,6))
UNSOLVED=(4,3)
ORDER=(4,1)


def slot(block):
    return "initialCode" if block["type"]=="cpp_compiler" else "code"


def runnable(block):
    src=block[slot(block)]
    if block["type"]=="fill_code":
        assert src.count("___")==1
        src=src.replace("___",block["answers"][0])
    return src


class AcumuladoresFormatoConOrdenHistorico(unittest.TestCase):
    def setUp(self):
        self.after=json.loads(PATH.read_text(encoding="utf-8"))
        self.meta=self.after["stateLayout"]
        self.originals={e["id"]:e["block"] for e in self.meta["sourceFormatRevisions"]}

    def test_historial_combinado_conserva_puzle_respuestas_y_progreso(self):
        state.validate(self.after)
        self.assertEqual(len(POSITIONS),len(self.originals))
        self.assertEqual(1,len(self.meta["orderRefinements"]))
        order=self.after["pages"][ORDER[0]]["blocks"][ORDER[1]]
        self.assertEqual("order_code",order["type"])
        original=deepcopy(self.after)
        original["stateLayout"].pop("sourceFormatRevisions")
        original["stateLayout"]["editorial"]=original["stateLayout"]["editorial"][:-len(POSITIONS)]
        for p,b in POSITIONS:
            identity=self.meta["current"][p]["blocks"][b]
            before,new=self.originals[identity],self.after["pages"][p]["blocks"][b]
            self.assertTrue(require_source_format(before,new),identity)
            key=slot(before)
            self.assertEqual([line for line in before[key].splitlines() if line.strip()],
                             [line for line in new[key].splitlines() if line.strip()],identity)
            original["pages"][p]["blocks"][b]=deepcopy(before)
        self.assertEqual(original["stateLayout"]["orderRefinements"],self.meta["orderRefinements"])
        self.assertEqual(original["orderVisualSeparators"],self.after["orderVisualSeparators"])
        self.assertEqual(original["pages"][ORDER[0]]["blocks"][ORDER[1]],order)
        state.validate(original)
        state.validate_transition(original,self.after)
        self.assertEqual(state.validate(original),state.validate(self.after))
        for field in ("origin","family","current","additions"):
            self.assertEqual(original["stateLayout"][field],self.meta[field])

    def test_cpp23_original_and_spaced_programs_match_without_solving_editor(self):
        with tempfile.TemporaryDirectory(prefix="accumulated-format-") as folder:
            for index,(p,b) in enumerate(POSITIONS):
                identity=self.meta["current"][p]["blocks"][b]
                before,after=self.originals[identity],self.after["pages"][p]["blocks"][b]
                cases=after.get("outputCheck",{}).get("cases",[])
                inputs=[x["standardInput"] for x in cases] or [after.get("initialStandardInput","")]
                variants=[]
                for label,block in (("historical",before),("formatted",after)):
                    source=Path(folder)/f"{index}-{label}.cpp"
                    exe=Path(folder)/f"{index}-{label}"
                    source.write_text(runnable(block),encoding="utf-8")
                    compiled=subprocess.run(["c++","-std=c++23","-O0","-o",str(exe),str(source)],
                                            capture_output=True,text=True,timeout=90)
                    self.assertEqual(0,compiled.returncode,identity+compiled.stderr)
                    runs=[]
                    for stdin in inputs:
                        result=subprocess.run([str(exe)],input=stdin,capture_output=True,text=True,timeout=15)
                        self.assertEqual(0,result.returncode,identity)
                        self.assertEqual("",result.stderr,identity)
                        runs.append((result.returncode,result.stdout,result.stderr))
                    variants.append(runs)
                self.assertEqual(variants[0],variants[1],identity)
                if after["type"]=="output_prediction":
                    self.assertIn(variants[1][0][1],after["answers"],identity)
                if (p,b)==UNSOLVED:
                    self.assertNotIn(variants[1][0][1],cases[0]["outputs"],identity)
                else:
                    for result,case in zip(variants[1],cases):
                        self.assertIn(result[1],case["outputs"],identity)


if __name__=="__main__":
    unittest.main()
