"""U6L14: preserve historical contracts; prove format-only code and optional inputs."""
from copy import deepcopy
import hashlib
import json
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest

import stable_state_layout as state
from code_format_compat import require_source_format

ROOT=Path(__file__).resolve().parents[2]
PATH=ROOT/"app/src/main/assets/content/es/programming/cpp_fundamentals/funciones_y_contratos/lesson_construir_funciones_desde_un_contrato.json"
EDITORIAL_BASE=5
SUFFIXES={
    "block_0004_0005": "\n\nEl primer caso de comprobación, Entrada «4 -10 0 6 10», debe mostrar estas dos líneas:\nNiveles: 0 0 6 6\nAl limite: 2\n\nSu comprobación correcta habilita «Continuar». Los siguientes casos son pruebas opcionales y no bloquean el paso; conserva el mismo programa y cambia únicamente Entrada.\n\nEntrada «0». Salida:\nNiveles:\nAl limite: 0\n\nEntrada «3 1 4 5». Salida:\nNiveles: 1 4 5\nAl limite: 0\n\nEntrada «1 -11». Salida:\nLecturas invalidas\n\nEntrada «1 11». Salida:\nLecturas invalidas\n\nEntrada «9 0 0 0 0 0 0 0 0 0». Salida:\nLecturas invalidas\n\nEntrada «1 6». Salida:\nNiveles: 6\nAl limite: 1",
    "block_0006_0005": "\n\nPara avanzar sólo necesitas comprobar correctamente el primer caso, Entrada «1 4». El segundo, Entrada «3 -2 3 6», es una prueba opcional y no bloquea el paso; conserva las salidas indicadas sin modificar las funciones."
}
OLD_TAIL="Pulsa «Compilar, ejecutar y comprobar» para verificar los nueve casos, conservando tu programa y cambiando sólo Entrada."
NEW_TAIL="Pulsa «Compilar, ejecutar y comprobar» con el primer caso, Entrada «4 -10 0 6 10». Su comprobación correcta habilita inmediatamente «Continuar». Los nueve casos siguen disponibles, pero los otros ocho son pruebas opcionales y no bloquean el paso. Conserva tu programa y cambia sólo Entrada."
REPLACED_ID="limite_cuatro_construccion_multicaso_formato"

class FunctionsFromContractFormatBatch(unittest.TestCase):
    def setUp(self):
        self.doc=json.loads(PATH.read_text(encoding="utf-8"))
        self.meta=self.doc["stateLayout"]
        self.before={entry["id"]:entry["block"] for entry in self.meta["sourceFormatRevisions"]}

    def test_historical_owners_and_lexical_format(self):
        state.validate(self.doc)
        self.assertEqual(4,len(self.before))
        previous=deepcopy(self.doc)
        previous["stateLayout"].pop("sourceFormatRevisions")
        previous["stateLayout"]["editorial"]=previous["stateLayout"]["editorial"][:EDITORIAL_BASE]
        for p,page in enumerate(self.doc["pages"]):
            for b,block in enumerate(page["blocks"]):
                ident=self.meta["current"][p]["blocks"][b]
                if ident in self.before:
                    old=self.before[ident]
                    self.assertTrue(require_source_format(old,block),ident)
                    slot="initialCode" if block["type"]=="cpp_compiler" else "code"
                    self.assertEqual([line for line in old[slot].splitlines() if line.strip()],
                                     [line for line in block[slot].splitlines() if line.strip()],ident)
                    self.assertGreater(block[slot].count("\n\n"),old[slot].count("\n\n"),ident)
                    for prop in ("answers","outputCheck","compileCheck"):
                        self.assertEqual(old.get(prop),block.get(prop),ident)
                    previous["pages"][p]["blocks"][b]=deepcopy(old)
                if ident in SUFFIXES:
                    extra=SUFFIXES[ident]
                    self.assertTrue(block["text"].endswith(extra),ident)
                    previous["pages"][p]["blocks"][b]["text"]=block["text"][:-len(extra)]
                if ident==REPLACED_ID:
                    self.assertTrue(block["text"].endswith(NEW_TAIL))
                    previous["pages"][p]["blocks"][b]["text"]=block["text"][:-len(NEW_TAIL)]+OLD_TAIL
                if ident in SUFFIXES or ident==REPLACED_ID:
                    desc=next(x for x in previous["textSemantics"]["fields"]
                              if x["block"]==ident and x["path"]=="/text")
                    old_text=previous["pages"][p]["blocks"][b]["text"]
                    desc["sourceSha256"]=hashlib.sha256(old_text.encode("utf-8")).hexdigest()
                    if ident==REPLACED_ID:
                        for span in desc["spans"]:
                            if span["text"]=="nueve":
                                span["start"]=old_text.index("nueve")
                                span["end"]=span["start"]+5
        self.assertEqual(EDITORIAL_BASE+len(self.before)+len(SUFFIXES)+1,len(self.meta["editorial"]))
        state.validate(previous)
        state.validate_transition(previous,self.doc)
        for key in ("origin","current","family","additions"):
            self.assertEqual(previous["stateLayout"][key],self.meta[key])

    def test_required_first_case_and_optional_outputs(self):
        first=self.doc["pages"][4]["blocks"][5]["text"]
        self.assertIn("primer caso",first)
        self.assertIn("pruebas opcionales",first)
        self.assertIn("no bloquean el paso",first)
        checks=self.doc["pages"][4]["blocks"][6]["outputCheck"]["cases"]
        self.assertEqual(7,len(checks))
        self.assertEqual("4 -10 0 6 10",checks[0]["standardInput"])
        for case in checks[1:]:
            self.assertIn("Entrada «"+case["standardInput"]+"». Salida:",first)
            self.assertIn(case["outputs"][0],first)
        two=self.doc["pages"][6]["blocks"][5]["text"]
        self.assertIn("primer caso",two)
        self.assertIn("prueba opcional",two)
        self.assertIn("no bloquea el paso",two)
        end=self.doc["pages"][6]["blocks"][10]["text"]
        self.assertIn("primer caso",end)
        self.assertIn("pruebas opcionales",end)
        self.assertIn("no bloquean el paso",end)
        cases=self.doc["pages"][6]["blocks"][20]["outputCheck"]["cases"]
        self.assertEqual(9,len(cases))
        descriptions="\n".join(x["text"] for x in self.doc["pages"][6]["blocks"][11:20])
        for case in cases:
            self.assertIn("Entrada «"+case["standardInput"]+"» → salida:",descriptions)
            self.assertIn(case["outputs"][0],descriptions)

    @unittest.skipUnless(shutil.which("c++"),"C++ compiler not available")
    def test_cpp23_reference_and_all_input_variants(self):
        cases={
            (4,2): [("", "Niveles: 0 4 6\nAl limite: 1")],
            (4,6): [(c["standardInput"],c["outputs"][0]) for c in
                     self.doc["pages"][4]["blocks"][6]["outputCheck"]["cases"]],
            (5,1): [("",self.doc["pages"][5]["blocks"][1]["answers"][0])],
            (6,23): [(c["standardInput"],c["outputs"][0]) for c in
                      self.doc["pages"][6]["blocks"][20]["outputCheck"]["cases"]]
        }
        with tempfile.TemporaryDirectory(prefix="u6l14-contract-") as folder:
            for (p,b),checks in cases.items():
                block=self.doc["pages"][p]["blocks"][b]
                ident=self.meta["current"][p]["blocks"][b]
                old=self.before[ident]
                slot="initialCode" if block["type"]=="cpp_compiler" else "code"
                variants=[]
                for tag,version in (("old",old),("new",block)):
                    src=Path(folder)/f"{p}-{b}-{tag}.cpp"
                    executable=Path(folder)/f"{p}-{b}-{tag}"
                    src.write_text(version[slot],encoding="utf-8")
                    proc=subprocess.run(["c++","-std=c++23","-O0",str(src),"-o",str(executable)],
                                        capture_output=True,text=True,timeout=90)
                    self.assertEqual(0,proc.returncode,ident+proc.stderr)
                    runs=[]
                    for inp,expected in checks:
                        run=subprocess.run([str(executable)],input=inp,capture_output=True,text=True,timeout=15)
                        self.assertEqual(0,run.returncode,ident+run.stderr)
                        self.assertEqual("",run.stderr,ident)
                        self.assertEqual(expected,run.stdout.rstrip("\n"),ident)
                        runs.append(run.stdout)
                    variants.append(runs)
                self.assertEqual(variants[0],variants[1],ident)

if __name__=="__main__":
    unittest.main()
