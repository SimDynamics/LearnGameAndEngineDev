"""Regresion U4L7 recorridos anidados: fuentes autenticas, casos y progreso."""
from copy import deepcopy
import json
from pathlib import Path
import subprocess
from windows_cpp_ci import msvc_cpp23_command, windows_executable
import tempfile
import unittest

import stable_state_layout as state
from code_format_compat import require_source_format, SourceFormatError
from format_batch_history import (
    Publication, all_owners, assert_format_history, assert_stable_storage,
    load_publication,
)

ROOT = Path(__file__).resolve().parents[2]
RELATIVE = (
    "app/src/main/assets/content/es/programming/cpp_fundamentals/"
    "repeticion_y_terminacion/lesson_recorridos_anidados.json"
)
PATH = ROOT / RELATIVE
PUBLICATIONS = {
    "before": Publication(
        "695d580ce966267bf2ab62560e609a4e4dff5c6a",
        "2ff953394fb1ff7e535ca5e69b82920232670faf780f51160bad109bb54f4a60",
    ),
    "formatted": Publication(
        "711b1e60a384f4267755b87168f1bf00f0c98032",
        "f11fc3334b7552bca7de377e5a08a4ffb6eb51057c4cb4112936689b682d07ad",
    ),
}
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
        original = load_publication(ROOT, RELATIVE, PUBLICATIONS["before"])
        formatted = load_publication(ROOT, RELATIVE, PUBLICATIONS["formatted"])
        for document in (original, formatted, self.current):
            state.validate(document)
        published = formatted["stateLayout"]
        ids = [published["current"][p]["blocks"][b] for p,b in POSITIONS]
        archives = published["sourceFormatRevisions"]
        self.assertEqual(len(POSITIONS), len(archives))
        self.assertEqual(ids, [row["id"] for row in archives])
        prefix = original["stateLayout"]["editorial"]
        self.assertEqual(published["editorial"][:len(prefix)], prefix)
        self.assertEqual(ids, [row["id"] for row in published["editorial"][len(prefix):]])
        # Later compatible edits may append edges. The exact Git publication
        # proves this batch; its authenticated history remains a prefix.
        self.assertEqual(self.meta["editorial"][:len(published["editorial"])],
                         published["editorial"])
        self.assertEqual(self.meta["sourceFormatRevisions"][:len(archives)], archives)
        before_blocks, after_blocks = all_owners(original), all_owners(formatted)
        for identity in ids:
            before, after = before_blocks[identity], after_blocks[identity]
            self.assertTrue(require_source_format(before, after), identity)
            key = source_field(before)
            self.assertEqual(
                [line for line in before[key].splitlines() if line.strip()],
                [line for line in after[key].splitlines() if line.strip()], identity)
        state.validate_transition(original, formatted)
        state.validate_transition(formatted, self.current)
        self.assertEqual(state.validate(original), state.validate(formatted))
        for field in ("origin","family","current","additions"):
            self.assertEqual(original["stateLayout"][field], published[field])
        assert_format_history(self, original, formatted, self.current, ids)
        assert_stable_storage(self, original, self.current, ids)

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
                    build = subprocess.run(msvc_cpp23_command(exe, src),
                                           capture_output=True,text=True,timeout=90)
                    self.assertEqual(0,build.returncode,identity+build.stderr)
                    outputs=[]
                    for stdin in inputs:
                        run = subprocess.run([str(windows_executable(exe))],input=stdin,
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
