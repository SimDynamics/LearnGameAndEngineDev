"""U5L1: fuentes vector, huecos, borradores y contrato historico autenticos."""
from copy import deepcopy
import json
from pathlib import Path
import subprocess
import tempfile
import unittest

import stable_state_layout as state
from code_format_compat import require_source_format

ROOT=Path(__file__).resolve().parents[2]
PATH=ROOT/"app/src/main/assets/content/es/programming/cpp_fundamentals/colecciones_y_recorridos/lesson_un_vector_para_una_coleccion.json"
POSITIONS=((0,3),(1,2),(1,3),(2,2),(3,2),(3,3),(3,4),
           (4,2),(5,2),(6,2),(7,1),(7,2),(7,7))
INCOMPLETE={(6,2)}


def field(block):
    if block["type"]=="cpp_compiler": return "initialCode"
    if block["type"]=="inline_fill_code": return "template"
    return "code"


def source(block):
    content=block[field(block)]
    if block["type"]=="fill_code":
        assert content.count("___")==1
        content=content.replace("___", block["answers"][0],1)
    if block["type"]=="inline_fill_code":
        for answer in block["answers"]:
            assert "[]" in content
            content=content.replace("[]",answer,1)
    return content


class VectorCollectionFormatBatch(unittest.TestCase):
    def setUp(self):
        self.current=json.loads(PATH.read_text(encoding="utf-8"))
        self.meta=self.current["stateLayout"]
        self.archive={x["id"]:x["block"] for x in self.meta["sourceFormatRevisions"]}

    def test_originals_keep_all_historical_answers_and_editor_addresses(self):
        state.validate(self.current)
        self.assertEqual(len(POSITIONS),len(self.archive))
        ids=[self.meta["current"][p]["blocks"][b] for p,b in POSITIONS]
        self.assertEqual(ids,[x["id"] for x in self.meta["editorial"][-len(POSITIONS):]])
        before=deepcopy(self.current)
        before["stateLayout"].pop("sourceFormatRevisions")
        before["stateLayout"]["editorial"]=before["stateLayout"]["editorial"][:-len(POSITIONS)]
        for p,b in POSITIONS:
            ident=self.meta["current"][p]["blocks"][b]
            original,updated=self.archive[ident],self.current["pages"][p]["blocks"][b]
            self.assertTrue(require_source_format(original,updated),ident)
            key=field(original)
            self.assertEqual([line for line in original[key].splitlines() if line.strip()],
                             [line for line in updated[key].splitlines() if line.strip()],ident)
            before["pages"][p]["blocks"][b]=deepcopy(original)
        state.validate(before)
        state.validate_transition(before,self.current)
        self.assertEqual(state.validate(before),state.validate(self.current))
        for key in ("origin","family","current","additions"):
            self.assertEqual(before["stateLayout"][key],self.meta[key])

    def test_old_and_new_cpp23_sources_match_for_all_optional_cases(self):
        with tempfile.TemporaryDirectory(prefix="u5l1-vector-format-") as folder:
            for idx,(p,b) in enumerate(POSITIONS):
                identity=self.meta["current"][p]["blocks"][b]
                old,new=self.archive[identity],self.current["pages"][p]["blocks"][b]
                cases=new.get("outputCheck",{}).get("cases",[])
                inputs=[item["standardInput"] for item in cases] or [
                    new.get("initialStandardInput","")]
                records=[]
                for label,block in (("original",old),("formatted",new)):
                    src=Path(folder)/f"{idx}-{label}.cpp"
                    exe=Path(folder)/f"{idx}-{label}"
                    src.write_text(source(block),encoding="utf-8")
                    compiled=subprocess.run(["c++","-std=c++23","-O0","-o",str(exe),str(src)],
                                            capture_output=True,text=True,timeout=90)
                    self.assertEqual(0,compiled.returncode,identity+compiled.stderr)
                    runs=[]
                    for stdin in inputs:
                        result=subprocess.run([str(exe)],input=stdin,capture_output=True,text=True,timeout=15)
                        self.assertEqual(0,result.returncode,identity)
                        self.assertEqual("",result.stderr,identity)
                        runs.append((result.returncode,result.stdout,result.stderr))
                    records.append(runs)
                self.assertEqual(records[0],records[1],identity)
                if new["type"]=="output_prediction":
                    self.assertIn(records[1][0][1],new["answers"],identity)
                if (p,b) in INCOMPLETE:
                    self.assertTrue(cases,identity)
                    self.assertNotIn(records[1][0][1],cases[0]["outputs"],identity)
                else:
                    for result,case in zip(records[1],cases):
                        self.assertIn(result[1],case["outputs"],identity)


if __name__=="__main__":
    unittest.main()
