"""U6L12: verify original code tokens, C++23 behavior, and optional input cases."""
from copy import deepcopy
import json
from pathlib import Path
import shutil
import subprocess
from windows_cpp_ci import msvc_cpp23_command, windows_executable
import tempfile
import unittest

import stable_state_layout as state
from code_format_compat import require_source_format

from format_batch_history import (
    Publication, assert_format_history, assert_stable_storage, compilable_source, load_publication,
)

ROOT=Path(__file__).resolve().parents[2]
PATH=ROOT/"app/src/main/assets/content/es/programming/cpp_fundamentals/funciones_y_contratos/lesson_tipo_de_retorno_explicito_y_deducido.json"
RELATIVE = PATH.relative_to(ROOT).as_posix()
PUBLICATIONS = {
    "before": Publication(
        "1a93e8c8a0871274f9df6e058514bfcb23a6aac0",
        "49989decde068df62d1f02a045ef35e54cf6424fea53f224e114e037bd6bf28e",
    ),
    "formatted": Publication(
        "b584b45696bfd2aff3954a0cae623cef7a5363ec",
        "69dc8002cdfec8317aaf028c78bfe057444a89e7c36508368cb844165e7113bd",
    ),
}
INTENTIONALLY_INVALID={(4,2),(5,2)}

class ReturnTypeFormatBatch(unittest.TestCase):
    def setUp(self):
        self.doc=json.loads(PATH.read_text(encoding="utf-8"))
        self.revisions={x["id"]:x["block"] for x in self.doc["stateLayout"]["sourceFormatRevisions"]}

    def test_immutable_contracts_and_source_tokens(self):
        current = self.doc
        current_sources = self.revisions
        publication_before = load_publication(ROOT, RELATIVE, PUBLICATIONS["before"])
        formatted = load_publication(ROOT, RELATIVE, PUBLICATIONS["formatted"])
        # The batch assertions describe this authenticated publication,
        # while later compatible revisions remain in the current lesson.
        self.doc = formatted
        self.revisions = {row["id"]: row["block"] for row in
                          formatted["stateLayout"]["sourceFormatRevisions"]}
        state.validate(self.doc)
        self.assertGreaterEqual(len(self.revisions),10)
        self.assertEqual(len(self.revisions),len(self.doc["stateLayout"]["sourceFormatRevisions"]))
        for p,page in enumerate(self.doc["pages"]):
            for b,block in enumerate(page["blocks"]):
                identity=self.doc["stateLayout"]["current"][p]["blocks"][b]
                old=self.revisions.get(identity)
                if old is None: continue
                self.assertTrue(require_source_format(old,block),identity)
                field="initialCode" if block["type"]=="cpp_compiler" else "code"
                self.assertEqual([line for line in old[field].splitlines() if line.strip()],
                                 [line for line in block[field].splitlines() if line.strip()],identity)
                self.assertGreater(block[field].count("\n\n"),old[field].count("\n\n"),identity)
                for key in ("outputCheck","compileCheck","answers"):
                    self.assertEqual(old.get(key),block.get(key),identity)

        state.validate(publication_before)
        state.validate_transition(publication_before, formatted)
        state.validate_transition(formatted, current)
        for metadata_key in ("editorial", "sourceFormatRevisions"):
            published = formatted["stateLayout"][metadata_key]
            self.assertEqual(current["stateLayout"][metadata_key][:len(published)], published)
        assert_format_history(self, publication_before, formatted, current, self.revisions)
        assert_stable_storage(self, publication_before, current, self.revisions)
        self.doc = current
        self.revisions = current_sources
        self.assertEqual(11,len(self.doc["pages"]))
        self.assertTrue(self.doc["compilerPuzzlesRequired"])

    def test_one_required_case_other_cases_optional(self):
        for p,b in ((6,2),(6,11),(7,6),(8,4),(8,17),(9,6)):
            paragraph=self.doc["pages"][p]["blocks"][b]["text"]
            self.assertTrue("primer caso" in paragraph or "primera Entrada" in paragraph, (p,b))
            self.assertIn("pruebas opcionales",paragraph)
            self.assertIn("no bloquean el paso",paragraph)
        scalar=self.doc["pages"][6]["blocks"][12]["outputCheck"]["cases"]
        collection=self.doc["pages"][8]["blocks"][18]["outputCheck"]["cases"]
        self.assertEqual(3,len(scalar))
        self.assertEqual(9,len(collection))
        self.assertEqual("-4 3",scalar[0]["standardInput"])
        self.assertEqual("0 4",collection[0]["standardInput"])
        for p,indices,cases in ((6,range(8,11),scalar),(8,range(8,17),collection)):
            descriptions="\n".join(self.doc["pages"][p]["blocks"][i]["text"] for i in indices)
            for case in cases:
                self.assertIn("Entrada: `"+case["standardInput"]+"`",descriptions)
                self.assertIn("`"+case["outputs"][0].split("\n")[0]+"`",descriptions)

    def test_cxx23_runs_before_and_after_equal(self):
        with tempfile.TemporaryDirectory(prefix="u6l12-returns-") as folder:
            for p,page in enumerate(self.doc["pages"]):
                for b,block in enumerate(page["blocks"]):
                    if (p,b) in INTENTIONALLY_INVALID: continue
                    identity=self.doc["stateLayout"]["current"][p]["blocks"][b]
                    old=self.revisions.get(identity)
                    if old is None: continue
                    variants=[]
                    cases=block.get("outputCheck",{}).get("cases",[])
                    inputs=[c["standardInput"] for c in cases] or [""]
                    for label,item in (("before",old),("after",block)):
                        source=Path(folder)/f"{p}-{b}-{label}.cpp"
                        exe=Path(folder)/f"{p}-{b}-{label}"
                        source.write_text(compilable_source(item),encoding="utf-8")
                        compile_result=subprocess.run(msvc_cpp23_command(exe, source),
                                                      capture_output=True,text=True,timeout=90)
                        self.assertEqual(0,compile_result.returncode,
                                         f"{identity} ({label})\n{compile_result.stdout}{compile_result.stderr}")
                        runs=[]
                        for data in inputs:
                            run=subprocess.run([str(windows_executable(exe))],input=data,capture_output=True,text=True,timeout=15)
                            runs.append((run.returncode,run.stdout,run.stderr))
                        variants.append(runs)
                    self.assertEqual(variants[0],variants[1],identity)

if __name__=="__main__":
    unittest.main()
