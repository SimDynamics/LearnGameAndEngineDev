"""U6L2: authentic C++23 behavior and historical state across 26 source format revisions."""
from copy import deepcopy
import json
from pathlib import Path
import subprocess
import tempfile
import unittest

import stable_state_layout as state
from code_format_compat import require_source_format

ROOT = Path(__file__).resolve().parents[2]
PATH = ROOT / "app/src/main/assets/content/es/programming/cpp_fundamentals/funciones_y_contratos/lesson_declaraciones_y_firmas.json"
POSITIONS = ((0,2),(1,2),(2,2),(3,2),(3,4),(4,2),(4,7),(5,3),(6,9),(7,5),(8,2),(8,8),(9,1),(9,7),(9,10),(10,4),(11,2),(11,5),(11,9),(11,12),(12,2),(13,2),(14,2),(15,2),(16,2),(17,2))
FILL_BLANKS = {(1,2)}
UNSOLVED_STARTERS = {(7,5), (9,7), (10,4), (11,9), (13,2)}
EXPECTED_COMPILE_FAILURES = {
    (14,2): ("u6-difference-unselected-definition", "undefined reference"),
    (15,2): ("u6-difference-conflicting-return", "diferencia"),
    (17,2): ("u6-difference-missing-header", "diferencia_ausente.h"),
}

def source_slot(block):
    return "initialCode" if block["type"] == "cpp_compiler" else "code"

class DeclarationsFormatBatch(unittest.TestCase):
    def setUp(self):
        self.lesson = json.loads(PATH.read_text(encoding="utf-8"))
        self.meta = self.lesson["stateLayout"]
        self.originals = {entry["id"]: entry["block"]
                          for entry in self.meta["sourceFormatRevisions"]}

    def test_preserve_originals_answers_and_progress(self):
        state.validate(self.lesson)
        self.assertEqual(26, len(POSITIONS))
        self.assertEqual(len(POSITIONS), len(self.originals))
        self.assertEqual([self.meta["current"][p]["blocks"][b] for p,b in POSITIONS],
                         [edge["id"] for edge in self.meta["editorial"][-len(POSITIONS):]])
        previous = deepcopy(self.lesson)
        previous["stateLayout"].pop("sourceFormatRevisions")
        previous["stateLayout"]["editorial"] = previous["stateLayout"]["editorial"][:-len(POSITIONS)]
        for p,b in POSITIONS:
            identity = self.meta["current"][p]["blocks"][b]
            old, new = self.originals[identity], self.lesson["pages"][p]["blocks"][b]
            self.assertTrue(require_source_format(old,new),identity)
            slot = source_slot(old)
            self.assertEqual([line for line in old[slot].splitlines() if line.strip()],
                             [line for line in new[slot].splitlines() if line.strip()],identity)
            self.assertGreater(new[slot].count("\n\n"),old[slot].count("\n\n"),identity)
            self.assertEqual(old.get("sourceFiles"),new.get("sourceFiles"),identity)
            self.assertEqual(old.get("outputCheck"),new.get("outputCheck"),identity)
            self.assertEqual(old.get("compileCheck"),new.get("compileCheck"),identity)
            old_files = {item["path"]: item["initialCode"]
                         for item in old.get("projectFiles", [])}
            new_files = {item["path"]: item["initialCode"]
                         for item in new.get("projectFiles", [])}
            self.assertEqual(old_files.keys(),new_files.keys(),identity)
            for path in old_files:
                self.assertEqual([line for line in old_files[path].splitlines() if line.strip()],
                                 [line for line in new_files[path].splitlines() if line.strip()],
                                 identity + ":" + path)
                if path == "main.cpp":
                    self.assertEqual(new["initialCode"], new_files[path], identity)
            previous["pages"][p]["blocks"][b] = deepcopy(old)
        state.validate(previous)
        state.validate_transition(previous,self.lesson)
        self.assertEqual(state.validate(previous),state.validate(self.lesson))
        for key in ("origin", "current", "family", "additions", "orderRefinements"):
            self.assertEqual(previous["stateLayout"][key],self.meta[key])
        self.assertEqual(previous["orderVisualSeparators"],
                         self.lesson["orderVisualSeparators"])

    def test_cpp23_originals_and_formatted_programs(self):
        with tempfile.TemporaryDirectory(prefix="declarations-u6l2-format-") as directory:
            for index,(p,b) in enumerate(POSITIONS):
                if (p,b) in FILL_BLANKS:
                    continue  # The deliberate fill-code placeholder is not C++ yet.
                identity = self.meta["current"][p]["blocks"][b]
                original = self.originals[identity]
                updated = self.lesson["pages"][p]["blocks"][b]
                cases = updated.get("outputCheck",{}).get("cases",[])
                inputs = [case["standardInput"] for case in cases] or [""]
                historical_runs = []
                for label,block in (("before",original),("after",updated)):
                    folder = Path(directory) / f"{index}-{label}"
                    folder.mkdir()
                    exe = folder / "program"
                    if block.get("projectFiles"):
                        for item in block["projectFiles"]:
                            source = folder / item["path"]
                            source.parent.mkdir(parents=True,exist_ok=True)
                            source.write_text(item["initialCode"],encoding="utf-8")
                        source_args = [str(folder / name) for name in block["sourceFiles"]]
                        command = ["c++","-std=c++23","-O0","-I",str(folder),
                                   *source_args,"-o",str(exe)]
                    else:
                        source = folder / "program.cpp"
                        source.write_text(block[source_slot(block)],encoding="utf-8")
                        command = ["c++","-std=c++23","-O0",str(source),"-o",str(exe)]
                    built = subprocess.run(command,capture_output=True,text=True,timeout=90)
                    failure = EXPECTED_COMPILE_FAILURES.get((p,b))
                    if failure:
                        self.assertEqual(failure[0],block["compileCheck"]["diagnostic"])
                        self.assertNotEqual(0,built.returncode,identity)
                        self.assertIn(failure[1],built.stderr,identity)
                        self.assertFalse(exe.exists(),identity)
                        historical_runs.append(("intentional compile/link failure",failure[0]))
                        continue
                    self.assertEqual(0,built.returncode,identity+built.stderr)
                    outputs = []
                    for data in inputs:
                        ran = subprocess.run([str(exe)],input=data,capture_output=True,
                                             text=True,timeout=15)
                        self.assertEqual(0,ran.returncode,identity+ran.stderr)
                        self.assertEqual("",ran.stderr,identity)
                        outputs.append((ran.returncode,ran.stdout,ran.stderr))
                    historical_runs.append(outputs)
                self.assertEqual(historical_runs[0],historical_runs[1],identity)
                if (p,b) in EXPECTED_COMPILE_FAILURES:
                    continue
                if updated["type"] == "output_prediction":
                    self.assertIn(historical_runs[1][0][1].rstrip("\n"),
                                  updated["answers"],identity)
                if cases:
                    for result,case in zip(historical_runs[1],cases):
                        if (p,b) in UNSOLVED_STARTERS:
                            self.assertNotIn(result[1],case["outputs"],identity)
                        else:
                            self.assertIn(result[1],case["outputs"],identity)

if __name__ == "__main__":
    unittest.main()
