"""U6L13: certify semantics, historical identity, source format and optional cases."""
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
PATH=ROOT/"app/src/main/assets/content/es/programming/cpp_fundamentals/funciones_y_contratos/lesson_hacer_explicito_el_orden_de_las_operaciones.json"
EDITORIAL_BASE=14
SUFFIXES={
    "registro_orden_propio_consigna": "\n\nLa comprobación correcta del primer caso, Entrada «2 7» con salida «9:27», habilita inmediatamente «Continuar». Las otras cuatro entradas y salidas indicadas a continuación son pruebas opcionales: puedes ejecutarlas para investigar, pero no bloquean el paso.",
    "block_0004_0007": "\n\nPara avanzar basta comprobar correctamente el primer caso, Entrada «2 7» y salida «9:27». La segunda entrada «9 1» con salida «10:91» es una prueba opcional y no bloquea el paso. Conserva el mismo código al probarla."
}
# Snapshots for codes are stored in sourceFormatRevisions; order fragments are not revised.

class OrderOfOperationsFormatBatch(unittest.TestCase):
    def setUp(self):
        self.doc=json.loads(PATH.read_text(encoding="utf-8"))
        self.meta=self.doc["stateLayout"]
        self.before={item["id"]:item["block"] for item in self.meta["sourceFormatRevisions"]}

    def test_historical_layout_and_lexical_equivalence(self):
        state.validate(self.doc)
        self.assertEqual(7,len(self.before))
        previous=deepcopy(self.doc)
        previous["stateLayout"].pop("sourceFormatRevisions")
        previous["stateLayout"]["editorial"]=previous["stateLayout"]["editorial"][:EDITORIAL_BASE]
        for p,page in enumerate(self.doc["pages"]):
            for b,block in enumerate(page["blocks"]):
                identity=self.meta["current"][p]["blocks"][b]
                if identity in self.before:
                    original=self.before[identity]
                    self.assertTrue(require_source_format(original,block),identity)
                    slot="initialCode" if block["type"]=="cpp_compiler" else "code"
                    self.assertEqual([line for line in original[slot].splitlines() if line.strip()],
                                     [line for line in block[slot].splitlines() if line.strip()],identity)
                    self.assertGreater(block[slot].count("\n\n"),original[slot].count("\n\n"),identity)
                    self.assertEqual(original.get("outputCheck"),block.get("outputCheck"))
                    previous["pages"][p]["blocks"][b]=deepcopy(original)
                if identity in SUFFIXES:
                    suffix=SUFFIXES[identity]
                    self.assertTrue(block["text"].endswith(suffix),identity)
                    restored=block["text"][:-len(suffix)]
                    previous["pages"][p]["blocks"][b]["text"]=restored
                    descriptor=next(field for field in previous["textSemantics"]["fields"]
                                    if field["block"]==identity and field["path"]=="/text")
                    descriptor["sourceSha256"]=hashlib.sha256(restored.encode("utf-8")).hexdigest()
        self.assertEqual(EDITORIAL_BASE+len(self.before)+len(SUFFIXES),len(self.meta["editorial"]))
        state.validate(previous)
        state.validate_transition(previous,self.doc)
        for key in ("origin","current","family","additions","orderRefinements"):
            self.assertEqual(previous["stateLayout"].get(key),self.meta.get(key))

    def test_first_case_only_and_single_line_order_cards(self):
        lesson=self.doc["pages"][3]["blocks"]
        tests=lesson[18]["outputCheck"]["cases"]
        self.assertEqual(5,len(tests))
        self.assertEqual("2 7",tests[0]["standardInput"])
        guide=lesson[12]["text"]
        self.assertIn("primer caso",guide)
        self.assertIn("pruebas opcionales",guide)
        self.assertIn("no bloquean el paso",guide)
        for case in tests:
            label="Entrada "+case["standardInput"]+" → salida "+case["outputs"][0]+"."
            self.assertTrue(any(block.get("text")==label for block in lesson),label)
        follow=self.doc["pages"][4]["blocks"][4]["text"]
        self.assertIn("primer caso",follow)
        self.assertIn("prueba opcional",follow)
        self.assertIn("no bloquea el paso",follow)
        self.assertEqual(2,len(self.doc["pages"][4]["blocks"][5]["outputCheck"]["cases"]))
        order=lesson[19]
        self.assertEqual("order_code",order["type"])
        self.assertEqual(len(order["fragments"]),len(order["correctOrder"]))
        self.assertEqual(set(range(len(order["fragments"]))),set(order["correctOrder"]))
        self.assertTrue(all("\n" not in fragment and "\r" not in fragment for fragment in order["fragments"]))
        self.assertEqual(1,len(self.meta["orderRefinements"]))

    @unittest.skipUnless(shutil.which("c++"),"No C++ compiler in this environment")
    def test_cpp23_source_equivalence_and_sequencing(self):
        cases={
            (1,1): [("",{"3:12","3:21"})], # + does not require one operand order
            (2,1): [("",{"3:12"})],
            (2,3): [("",{"3:21"})],
            (3,6): [("",{"9:27"})],
            (3,22): [("2 7",{"9:27"}),("7 2",{"9:72"}),("1 1",{"2:11"}),("9 1",{"10:91"}),("1 9",{"10:19"})],
            (3,25): [("",{"9:72"})],
            (4,5): [("2 7",{"9:27"}),("9 1",{"10:91"})]
        }
        with tempfile.TemporaryDirectory(prefix="u6l13-order-") as folder:
            for (p,b),checks in cases.items():
                block=self.doc["pages"][p]["blocks"][b]
                identity=self.meta["current"][p]["blocks"][b]
                original=self.before[identity]
                slot="initialCode" if block["type"]=="cpp_compiler" else "code"
                outcomes=[]
                for tag,version in (("old",original),("new",block)):
                    source=Path(folder)/f"{p}-{b}-{tag}.cpp"
                    binary=Path(folder)/f"{p}-{b}-{tag}"
                    source.write_text(version[slot],encoding="utf-8")
                    proc=subprocess.run(["c++","-std=c++23","-O0",str(source),"-o",str(binary)],
                                        capture_output=True,text=True,timeout=90)
                    self.assertEqual(0,proc.returncode,identity+proc.stderr)
                    results=[]
                    for inp,accepted in checks:
                        run=subprocess.run([str(binary)],input=inp,capture_output=True,text=True,timeout=15)
                        self.assertEqual(0,run.returncode,identity+run.stderr)
                        self.assertEqual("",run.stderr,identity)
                        self.assertIn(run.stdout.rstrip("\n"),accepted,identity)
                        results.append(run.stdout)
                    outcomes.append(results)
                if (p,b)!=(1,1):
                    self.assertEqual(outcomes[0],outcomes[1],identity)

if __name__=="__main__":
    unittest.main()
