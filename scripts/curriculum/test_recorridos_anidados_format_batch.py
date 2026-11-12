"""Regresion U4L7 recorridos anidados: fuentes autenticas, casos y progreso."""
from copy import deepcopy
import json
from pathlib import Path
import subprocess
import tempfile
import unittest

import stable_state_layout as state
from code_format_compat import require_source_format, SourceFormatError

ROOT = Path(__file__).resolve().parents[2]
PATH = ROOT / "app/src/main/assets/content/es/programming/cpp_fundamentals/repeticion_y_terminacion/lesson_recorridos_anidados.json"
POSITIONS = (
    (0,2),(0,4),(1,2),(1,3),(2,1),(2,2),(2,3),(3,2),
    (4,1),(4,2),(5,2),(6,2),(6,6),(7,4),(8,2),(8,8),
    (9,2),(9,7),(10,4),(11,2),(11,5),(11,10)
)
# Estos editores requieren trabajo del estudiante y no deben acreditarse con el inicial.
INCOMPLETE = {(5,2), (7,4), (9,7), (10,4)}


def source_field(block):
    if block["type"] == "cpp_compiler":
        return "initialCode"
    if block["type"] in ("inline_fill_code", "drag_fill_code"):
        return "template"
    return "code"


def runnable_source(block):
    source = block[source_field(block)]
    if block["type"] == "fill_code":
        assert source.count("___") == 1
        source = source.replace("___", block["answers"][0], 1)
    return source


class NestedLoopFormatBatch(unittest.TestCase):
    def setUp(self):
        self.current = json.loads(PATH.read_text(encoding="utf-8"))
        self.meta = self.current["stateLayout"]
        self.archived = {row["id"]: row["block"] for row in
                         self.meta["sourceFormatRevisions"]}

    def test_authenticated_revision_retains_identifiers_answers_and_drafts(self):
        state.validate(self.current)
        self.assertEqual(len(POSITIONS), len(self.archived))
        ids = [self.meta["current"][p]["blocks"][b] for p,b in POSITIONS]
        self.assertEqual(ids, [r["id"] for r in self.meta["editorial"][-len(POSITIONS):]])
        original = deepcopy(self.current)
        original["stateLayout"].pop("sourceFormatRevisions")
        original["stateLayout"]["editorial"] = original["stateLayout"]["editorial"][:-len(POSITIONS)]
        for p,b in POSITIONS:
            identity = self.meta["current"][p]["blocks"][b]
            before, after = self.archived[identity], self.current["pages"][p]["blocks"][b]
            self.assertTrue(require_source_format(before, after), identity)
            key = source_field(before)
            self.assertEqual(
                [line for line in before[key].splitlines() if line.strip()],
                [line for line in after[key].splitlines() if line.strip()], identity)
            original["pages"][p]["blocks"][b] = deepcopy(before)
        state.validate(original)
        state.validate_transition(original, self.current)
        self.assertEqual(state.validate(original), state.validate(self.current))
        for field in ("origin","family","current","additions"):
            self.assertEqual(original["stateLayout"][field], self.meta[field])

    def test_cpp23_executes_before_and_after_with_same_outputs(self):
        with tempfile.TemporaryDirectory(prefix="nested-loop-format-") as folder:
            for ordinal,(p,b) in enumerate(POSITIONS):
                identity = self.meta["current"][p]["blocks"][b]
                old, new = self.archived[identity], self.current["pages"][p]["blocks"][b]
                cases = new.get("outputCheck",{}).get("cases",[])
                inputs = [x["standardInput"] for x in cases] or [new.get("initialStandardInput","")]
                records = []
                for label, block in (("old",old),("new",new)):
                    src = Path(folder)/f"{ordinal}-{label}.cpp"
                    exe = Path(folder)/f"{ordinal}-{label}"
                    src.write_text(runnable_source(block),encoding="utf-8")
                    build = subprocess.run(["c++","-std=c++23","-O0","-o",str(exe),str(src)],
                                           capture_output=True,text=True,timeout=90)
                    self.assertEqual(0,build.returncode,identity+build.stderr)
                    outputs=[]
                    for stdin in inputs:
                        run = subprocess.run([str(exe)],input=stdin,
                                              capture_output=True,text=True,timeout=15)
                        self.assertEqual(0,run.returncode,identity)
                        self.assertEqual("",run.stderr,identity)
                        outputs.append((run.returncode,run.stdout,run.stderr))
                    records.append(outputs)
                self.assertEqual(records[0],records[1],identity)
                if new["type"] == "output_prediction":
                    self.assertIn(records[1][0][1],new["answers"],identity)
                if (p,b) in INCOMPLETE:
                    self.assertTrue(cases,identity)
                    self.assertNotIn(records[1][0][1],cases[0]["outputs"],identity)
                else:
                    for result,case in zip(records[1],cases):
                        self.assertIn(result[1],case["outputs"],identity)

    def test_logic_and_archived_provenance_cannot_be_forged(self):
        identity = self.meta["current"][0]["blocks"][2]
        before, after = self.archived[identity], self.current["pages"][0]["blocks"][2]
        attack = deepcopy(after)
        attack["code"] = attack["code"].replace("++fila", "--fila")
        with self.assertRaises(SourceFormatError):
            require_source_format(before, attack)
        forged = deepcopy(self.current)
        forged["stateLayout"]["sourceFormatRevisions"][0]["block"]["code"] = "int main(){return 0;}"
        with self.assertRaises(state.LayoutError):
            state.validate(forged)


if __name__ == "__main__":
    unittest.main()
