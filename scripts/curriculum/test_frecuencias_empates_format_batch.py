"""U5L13: frequency ties and authentic old and new C++23 programs."""
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
PATH=ROOT/"app/src/main/assets/content/es/programming/cpp_fundamentals/colecciones_y_recorridos/lesson_contar_frecuencias_y_conservar_empates.json"
PUBLICATIONS={
    "before": Publication("0e66f3dd98960fe1dcd09978a1037488d3ff9f06",
        "8a15cd21302769df9d6e5b5bf9d3199927ed2545261af5ea569ccc03fc043a9f"),
    "formatted": Publication("ca297e87c79e606495cf76ebd9f59a3bd2a42c2d",
        "d1ad0cfbcbde09a4c5eda9f4f101c3dbb0b00eb336fe50fef764f8a4be6fd959"),
}
POSITIONS=((1,2),(2,5),(3,2),(3,9),(4,2),(4,4),(5,5),(5,16),
           (6,2),(6,10),(6,13),(7,2),(7,4),(7,10),(7,19),(7,22))
UNSOLVED={(2,5),(5,5),(5,16),(7,10),(7,19)}

def slot(block):
    return "initialCode" if block["type"]=="cpp_compiler" else "code"

class FrequencyFormatBatch(unittest.TestCase):
    def setUp(self):
        self.current=json.loads(PATH.read_text(encoding="utf-8"))
        self.meta=self.current["stateLayout"]
        self.originals={entry["id"]:entry["block"] for entry in self.meta["sourceFormatRevisions"]}

    def test_historical_state_and_source_are_unchanged(self):
        relative=PATH.relative_to(ROOT).as_posix()
        previous_publication=load_publication(ROOT,relative,PUBLICATIONS["before"])
        formatted=load_publication(ROOT,relative,PUBLICATIONS["formatted"])
        for document in (previous_publication,formatted,self.current):
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
            ident=published["current"][p]["blocks"][b]
            old,new=before_blocks[ident],formatted["pages"][p]["blocks"][b]
            self.assertTrue(require_source_format(old,new),ident)
            key=slot(old)
            self.assertEqual([line for line in old[key].splitlines() if line.strip()],
                             [line for line in new[key].splitlines() if line.strip()],ident)
            self.assertEqual(old[key].count("} else if ("),new[key].count("} else if ("),ident)
        state.validate_transition(previous_publication,formatted)
        state.validate_transition(formatted,self.current)
        self.assertEqual(state.validate(previous_publication),state.validate(formatted))
        for metadata_key in ("origin","family","current","additions"):
            self.assertEqual(previous_publication["stateLayout"][metadata_key],
                             published[metadata_key])
        assert_format_history(self,previous_publication,formatted,self.current,ids)
        assert_stable_storage(self,previous_publication,self.current,ids)

    def test_cpp23_same_outputs_and_optional_oracles(self):
        with tempfile.TemporaryDirectory(prefix="frequency-ties-format-") as folder:
            for i,(p,b) in enumerate(POSITIONS):
                ident=self.meta["current"][p]["blocks"][b]
                old,new=self.originals[ident],self.current["pages"][p]["blocks"][b]
                cases=new.get("outputCheck",{}).get("cases",[])
                inputs=[case["standardInput"] for case in cases] or [new.get("initialStandardInput","")]
                variants=[]
                for tag,block in (("before",old),("after",new)):
                    source=Path(folder)/f"{i}-{tag}.cpp"
                    exe=Path(folder)/f"{i}-{tag}"
                    source.write_text(block[slot(block)],encoding="utf-8")
                    built=subprocess.run(msvc_cpp23_command(exe, source),
                                         capture_output=True,text=True,timeout=90)
                    self.assertEqual(0,built.returncode,ident+built.stderr)
                    outputs=[]
                    for data in inputs:
                        run=subprocess.run([str(windows_executable(exe))],input=data,capture_output=True,text=True,timeout=15)
                        self.assertEqual(0,run.returncode,ident)
                        self.assertEqual("",run.stderr,ident)
                        outputs.append((run.returncode,run.stdout,run.stderr))
                    variants.append(outputs)
                self.assertEqual(variants[0],variants[1],ident)
                if new["type"]=="output_prediction":
                    self.assertIn(variants[1][0][1].rstrip("\n"),new["answers"],ident)
                if (p,b) in UNSOLVED:
                    self.assertTrue(cases,ident)
                    self.assertNotIn(variants[1][0][1],cases[0]["outputs"],ident)
                else:
                    for result,case in zip(variants[1],cases):
                        self.assertIn(result[1],case["outputs"],ident)

if __name__=="__main__":
    unittest.main()
