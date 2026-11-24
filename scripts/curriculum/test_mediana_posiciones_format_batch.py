"""U5L11 median, even/odd central positions and authentic historical editors."""
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
    Publication, all_owners, assert_format_history, assert_stable_storage,
    load_publication,
)

ROOT=Path(__file__).resolve().parents[2]
PATH=ROOT/"app/src/main/assets/content/es/programming/cpp_fundamentals/colecciones_y_recorridos/lesson_mediana_y_posiciones_centrales.json"
PUBLICATIONS={
    "before": Publication("d7ee91c7f3138dbbe690aa557473f93e04134a6a",
        "7c2fb7d4dc7799ddddb99b151be9372aa166e1b504040439cc18e5db04e8ba17"),
    "formatted": Publication("b19289c6cd66a97e308e86b95c3e710a2a8e6436",
        "c2ac586a738575769ba9c8e05d5f565ab23b4b075b174d42998aa3a5debbefc2"),
}
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
        relative=PATH.relative_to(ROOT).as_posix()
        previous_publication=load_publication(ROOT,relative,PUBLICATIONS["before"])
        formatted=load_publication(ROOT,relative,PUBLICATIONS["formatted"])
        for document in (previous_publication,formatted,self.after):
            state.validate(document)
        published=formatted["stateLayout"]
        ids=[published["current"][p]["blocks"][b] for p,b in POSITIONS]
        archives=published["sourceFormatRevisions"]
        self.assertEqual(len(POSITIONS),len(archives))
        self.assertEqual(ids,[entry["id"] for entry in archives])
        prefix=previous_publication["stateLayout"]["editorial"]
        self.assertEqual(published["editorial"][:len(prefix)],prefix)
        self.assertEqual(ids,[entry["id"] for entry in published["editorial"][len(prefix):]])
        # Pin this batch to its complete publication; later compatible edits append history.
        self.assertEqual(self.meta["editorial"][:len(published["editorial"])],
                         published["editorial"])
        self.assertEqual(self.meta["sourceFormatRevisions"][:len(archives)],archives)
        before_blocks=all_owners(previous_publication)
        for p,b in POSITIONS:
            id_=published["current"][p]["blocks"][b]
            old,new=before_blocks[id_],formatted["pages"][p]["blocks"][b]
            self.assertTrue(require_source_format(old,new),id_)
            slot=field(old)
            self.assertEqual([line for line in old[slot].splitlines() if line.strip()],
                             [line for line in new[slot].splitlines() if line.strip()],id_)
            self.assertEqual(old[slot].count("static_cast<double>"),
                             new[slot].count("static_cast<double>"),id_)
        state.validate_transition(previous_publication,formatted)
        state.validate_transition(formatted,self.after)
        self.assertEqual(state.validate(previous_publication),state.validate(formatted))
        for metadata_key in ("origin","family","current","additions"):
            self.assertEqual(previous_publication["stateLayout"][metadata_key],
                             published[metadata_key])
        assert_format_history(self,previous_publication,formatted,self.after,ids)
        assert_stable_storage(self,previous_publication,self.after,ids)

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
                    built=subprocess.run(msvc_cpp23_command(binary, source),
                                         capture_output=True,text=True,timeout=90)
                    self.assertEqual(0,built.returncode,id_+built.stderr)
                    observations=[]
                    for stdin in inputs:
                        executed=subprocess.run([str(windows_executable(binary))],input=stdin,capture_output=True,text=True,timeout=15)
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
