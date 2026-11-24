"""U6L2: authentic C++23 behavior and historical state across 26 source format revisions."""
from copy import deepcopy
import json
from pathlib import Path
import subprocess
from windows_cpp_ci import msvc_cpp23_command, windows_executable
import tempfile
import unittest

import stable_state_layout as state
from code_format_compat import require_source_format

from format_batch_history import (
    Publication, assert_format_history, assert_stable_storage, load_publication,
)

ROOT = Path(__file__).resolve().parents[2]
PATH = ROOT / "app/src/main/assets/content/es/programming/cpp_fundamentals/funciones_y_contratos/lesson_declaraciones_y_firmas.json"
RELATIVE = PATH.relative_to(ROOT).as_posix()
PUBLICATIONS = {
    "before": Publication(
        "94e6d9685ad7403aada42717dd4ee41da7baa157",
        "92ca7596b32d35d17b9361879045f103cc41a8e9ac1b1822f8b8cb61307917fb",
    ),
    "formatted": Publication(
        "700b4e16720d8c942a5bc95f0a2c1f3d1e6e88f4",
        "eaae140880a99040e71462b4989890c9944b42cc555cd7b729d91262ece6054e",
    ),
}
POSITIONS = ((0,2),(1,2),(2,2),(3,2),(3,4),(4,2),(4,7),(5,3),(6,9),(7,5),(8,2),(8,8),(9,1),(9,7),(9,10),(10,4),(11,2),(11,5),(11,9),(11,12),(12,2),(13,2),(16,2))
FILL_BLANKS = {(1,2)}
UNSOLVED_STARTERS = {(7,5), (9,7), (10,4), (11,9), (13,2)}
EXPECTED_COMPILE_FAILURES = {
    (14,2): ("u6-difference-unselected-definition", "LNK2019"),
    (15,2): ("u6-difference-conflicting-return", "diferencia"),
    (17,2): ("u6-difference-missing-header", "diferencia_ausente.h"),
}

def source_slot(block):
    return "initialCode" if block["type"] == "cpp_compiler" else "code"

class DeclarationsFormatBatch(unittest.TestCase):
    def setUp(self):
        self.lesson = json.loads(PATH.read_text(encoding="utf-8"))
        self.meta = self.lesson["stateLayout"]
        # This batch has 23 owners; later diagnostic formatting is
        # validated below and still compiles from the current lesson.
        batch = load_publication(ROOT, RELATIVE, PUBLICATIONS["formatted"])
        self.originals = {entry["id"]: entry["block"]
                          for entry in batch["stateLayout"]["sourceFormatRevisions"]}

    def test_preserve_originals_answers_and_progress(self):
        current = self.lesson
        current_sources = self.originals
        publication_before = load_publication(ROOT, RELATIVE, PUBLICATIONS["before"])
        formatted = load_publication(ROOT, RELATIVE, PUBLICATIONS["formatted"])
        # The batch assertions describe this authenticated publication,
        # while later compatible revisions remain in the current lesson.
        self.lesson = formatted
        self.meta = formatted["stateLayout"]
        self.originals = {row["id"]: row["block"] for row in
                          formatted["stateLayout"]["sourceFormatRevisions"]}
        state.validate(self.lesson)
        self.assertEqual(23, len(POSITIONS))
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

        self.assertEqual(previous, publication_before)
        state.validate(publication_before)
        state.validate_transition(publication_before, formatted)
        state.validate_transition(formatted, current)
        for metadata_key in ("editorial", "sourceFormatRevisions"):
            published = formatted["stateLayout"][metadata_key]
            self.assertEqual(current["stateLayout"][metadata_key][:len(published)], published)
        assert_format_history(self, publication_before, formatted, current, self.originals)
        assert_stable_storage(self, publication_before, current, self.originals)
        self.lesson = current
        self.originals = current_sources
        self.meta = current["stateLayout"]

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
                        command = msvc_cpp23_command(exe, *source_args, include=folder)
                    else:
                        source = folder / "program.cpp"
                        source.write_text(block[source_slot(block)],encoding="utf-8")
                        command = msvc_cpp23_command(exe, source)
                    built = subprocess.run(command,capture_output=True,text=True,timeout=90)
                    failure = EXPECTED_COMPILE_FAILURES.get((p,b))
                    if failure:
                        self.assertEqual(failure[0],block["compileCheck"]["diagnostic"])
                        self.assertNotEqual(0,built.returncode,identity)
                        self.assertIn(failure[1],built.stdout+built.stderr,identity)
                        self.assertFalse(windows_executable(exe).exists(),identity)
                        historical_runs.append(("intentional compile/link failure",failure[0]))
                        continue
                    self.assertEqual(0,built.returncode,identity+built.stderr)
                    outputs = []
                    for data in inputs:
                        ran = subprocess.run([str(windows_executable(exe))],input=data,capture_output=True,
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

    def test_unchanged_diagnostic_programs_still_fail_for_the_expected_reason(self):
        """Diagnostics are historically byte-exact; format is never a reason to rewrite them."""
        with tempfile.TemporaryDirectory(prefix="u6l2-protected-diagnostics-") as directory:
            for index,((p,b),(diagnostic,marker)) in enumerate(EXPECTED_COMPILE_FAILURES.items()):
                identity = self.meta["current"][p]["blocks"][b]
                self.assertNotIn(identity,self.originals)
                block = self.lesson["pages"][p]["blocks"][b]
                self.assertEqual(diagnostic,block["compileCheck"]["diagnostic"])
                self.assertEqual(block["initialCode"],
                                 next(item["initialCode"] for item in block["projectFiles"]
                                      if item["path"]=="main.cpp"))
                folder = Path(directory)/str(index)
                folder.mkdir()
                for item in block["projectFiles"]:
                    (folder/item["path"]).write_text(item["initialCode"],encoding="utf-8")
                exe=folder/"program"
                command=msvc_cpp23_command(
                    exe, *(folder/name for name in block["sourceFiles"]), include=folder)
                result=subprocess.run(command,capture_output=True,text=True,timeout=90)
                self.assertNotEqual(0,result.returncode,identity)
                self.assertIn(marker,result.stdout+result.stderr,identity)
                self.assertFalse(windows_executable(exe).exists(),identity)

if __name__ == "__main__":
    unittest.main()
