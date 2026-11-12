"""Valida U4L4: separación física sin cambiar selección, resumen ni acertijos."""
from copy import deepcopy
import json
from pathlib import Path
import subprocess
import tempfile
import unittest

import stable_state_layout as state
from code_format_compat import require_source_format

ROOT = Path(__file__).resolve().parents[2]
LESSON = ROOT / "app/src/main/assets/content/es/programming/cpp_fundamentals/repeticion_y_terminacion/lesson_seleccionar_durante_un_recorrido.json"
POSITIONS = ((0,2),(0,4),(1,2),(1,3),(2,2),(2,3),(2,4),(3,3),(4,2),(5,2),(5,6))
INCOMPLETE = (4,2)


def field(block):
    if block["type"] == "cpp_compiler": return "initialCode"
    if block["type"] in ("inline_fill_code", "drag_fill_code"): return "template"
    return "code"


def runnable(block):
    source = block[field(block)]
    if block["type"] == "fill_code":
        assert source.count("___") == 1
        source = source.replace("___", block["answers"][0])
    return source


class SeleccionarDuranteFormato(unittest.TestCase):
    def setUp(self):
        self.doc = json.loads(LESSON.read_text(encoding="utf-8"))
        self.state = self.doc["stateLayout"]
        self.archive = {r["id"]:r["block"] for r in self.state["sourceFormatRevisions"]}

    def test_preserves_real_editorial_history(self):
        state.validate(self.doc)
        self.assertEqual(len(POSITIONS),len(self.archive))
        original=deepcopy(self.doc)
        original["stateLayout"].pop("sourceFormatRevisions")
        original["stateLayout"]["editorial"] = original["stateLayout"]["editorial"][:-len(POSITIONS)]
        for p,b in POSITIONS:
            ident=self.state["current"][p]["blocks"][b]
            before,after=self.archive[ident],self.doc["pages"][p]["blocks"][b]
            self.assertTrue(require_source_format(before,after),ident)
            slot=field(before)
            self.assertEqual([s for s in before[slot].splitlines() if s.strip()],
                             [s for s in after[slot].splitlines() if s.strip()],ident)
            original["pages"][p]["blocks"][b] = deepcopy(before)
        state.validate(original)
        state.validate_transition(original,self.doc)
        self.assertEqual(state.validate(original),state.validate(self.doc))
        for key in ("origin","family","current","additions"):
            self.assertEqual(original["stateLayout"][key],self.state[key])

    def test_same_cpp23_executions_preserve_incomplete_initial_editor(self):
        with tempfile.TemporaryDirectory(prefix="u4l4-spacing-") as folder:
            for index,(p,b) in enumerate(POSITIONS):
                ident=self.state["current"][p]["blocks"][b]
                old,new=self.archive[ident],self.doc["pages"][p]["blocks"][b]
                cases=new.get("outputCheck",{}).get("cases",[])
                inputs=[case["standardInput"] for case in cases] or [new.get("initialStandardInput","")]
                observations=[]
                for label,bl in (("old",old),("new",new)):
                    src=Path(folder)/f"{index}-{label}.cpp"
                    exe=Path(folder)/f"{index}-{label}"
                    src.write_text(runnable(bl),encoding="utf-8")
                    build=subprocess.run(["c++","-std=c++23","-O0","-o",str(exe),str(src)],
                                         capture_output=True,text=True,timeout=90)
                    self.assertEqual(0,build.returncode,ident+build.stderr)
                    results=[]
                    for stdin in inputs:
                        result=subprocess.run([str(exe)],input=stdin,capture_output=True,text=True,timeout=15)
                        self.assertEqual(0,result.returncode,ident)
                        self.assertEqual("",result.stderr,ident)
                        results.append((result.returncode,result.stdout,result.stderr))
                    observations.append(results)
                self.assertEqual(observations[0],observations[1],ident)
                if new["type"] == "output_prediction":
                    self.assertIn(observations[1][0][1],new["answers"],ident)
                if (p,b)==INCOMPLETE:
                    self.assertNotIn(observations[1][0][1],cases[0]["outputs"],
                                     "An incomplete student program cannot be treated as solved")
                else:
                    for result,case in zip(observations[1],cases):
                        self.assertIn(result[1],case["outputs"],ident)


if __name__ == "__main__":
    unittest.main()
