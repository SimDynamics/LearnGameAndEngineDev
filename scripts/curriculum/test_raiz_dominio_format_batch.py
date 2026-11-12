"""U6L9: sqrt domain, approximate properties, historic state, and optional cases."""
from copy import deepcopy
import json
from pathlib import Path
import subprocess
import tempfile
import unittest

import stable_state_layout as state
from code_format_compat import require_source_format

ROOT=Path(__file__).resolve().parents[2]
PATH=ROOT/"app/src/main/assets/content/es/programming/cpp_fundamentals/funciones_y_contratos/lesson_raiz_cuadrada_dominio_y_resultado_aproximado.json"
POSITIONS=((0,4),(0,5),(1,3),(2,3),(2,5),(3,5),(4,2),(4,7),(4,10),(5,2),(5,4),(6,5),(6,16),(7,2),(7,4),(7,8))
PROSE=((4,6),(6,15),(7,7))
OLD_PARAGRAPHS=json.loads(r'''{"block_0004_0006":"Este programa lee un área finita y prepara destino menos tres. Comprueba `0.0` → `1:0`, `100.0` → `1:10`, `16.0` → `1:4`, `0.25` → `1:0.5`; `-1.0` y `100.5` → `0:-3`. El cero aceptado reemplaza el destino; los rechazos lo conservan.\n\nPulsa «Compilar, ejecutar y comprobar». Conserva el programa al variar sólo Entrada entre los casos declarados. El salto después del último mensaje es opcional.","raiz_coleccion_propia_accion":"Conserva las mismas funciones y cambia sólo Entrada entre las seis pruebas. Pulsa «Compilar, ejecutar y comprobar» para cada caso y compara las cuatro líneas completas. Los resultados deben proceder de las llamadas y de la colección examinada.","block_0007_0007":"El programa lee primero una cantidad no negativa y después ese número de valores escritos válidamente. Algunos casos incumplen deliberadamente el dominio de la función y deben rechazarse antes del cálculo. El bucle de preparación construye la colección; las funciones conservan sus contratos. Comprueba estas entradas: `0`; `1 0.0`; `1 -1.0`; `1 101.0`; `3 100.0 6.25 9.0`; `3 2.0 -2.0 102.0`. Conserva hasta ocho áreas finitas de menos dos a ciento dos. Un rechazo no participa en el conteo de lados y la frontera seis con veinticinco debe incluirse.\n\nPulsa «Compilar, ejecutar y comprobar». Conserva el programa al variar sólo Entrada entre los casos declarados. El salto después del último mensaje es opcional."}''')
OLD_SEMANTICS=json.loads(r'''{"block_0004_0006":{"block":"block_0004_0006","path":"/text","sourceSha256":"767006b5dff97a86ea68d2fccadda6ef91a938bf29655a899518792f57044f37","spans":[{"start":51,"end":61,"text":"menos tres","role":"data","reading":"natural"},{"start":74,"end":77,"text":"0.0","role":"data","reading":"natural"},{"start":82,"end":85,"text":"1:0","role":"output","reading":"natural"},{"start":89,"end":94,"text":"100.0","role":"data","reading":"natural"},{"start":99,"end":103,"text":"1:10","role":"output","reading":"natural"},{"start":107,"end":111,"text":"16.0","role":"data","reading":"natural"},{"start":116,"end":119,"text":"1:4","role":"output","reading":"natural"},{"start":123,"end":127,"text":"0.25","role":"data","reading":"natural"},{"start":132,"end":137,"text":"1:0.5","role":"output","reading":"natural"},{"start":141,"end":145,"text":"-1.0","role":"data","reading":"natural"},{"start":150,"end":155,"text":"100.5","role":"data","reading":"natural"},{"start":160,"end":164,"text":"0:-3","role":"output","reading":"natural"},{"start":170,"end":174,"text":"cero","role":"data","reading":"natural"}]},"raiz_coleccion_propia_accion":{"block":"raiz_coleccion_propia_accion","path":"/text","sourceSha256":"eea251b41528b8f0ba0c467ca2ed1444eceb835651201e9c2dc4d9e550b33f38","spans":[{"start":62,"end":66,"text":"seis","role":"data","reading":"natural"},{"start":144,"end":150,"text":"cuatro","role":"data","reading":"natural"}]},"block_0007_0007":{"block":"block_0007_0007","path":"/text","sourceSha256":"bb6373a94aff0f747533c1948d5121b0ff4c1ee87b73d3b0f1cf6f41201f62a1","spans":[{"start":320,"end":321,"text":"0","role":"data","reading":"natural"},{"start":325,"end":330,"text":"1 0.0","role":"data","reading":"natural"},{"start":334,"end":340,"text":"1 -1.0","role":"data","reading":"natural"},{"start":344,"end":351,"text":"1 101.0","role":"data","reading":"natural"},{"start":355,"end":371,"text":"3 100.0 6.25 9.0","role":"data","reading":"natural"},{"start":375,"end":391,"text":"3 2.0 -2.0 102.0","role":"data","reading":"natural"},{"start":409,"end":413,"text":"ocho","role":"data","reading":"natural"},{"start":431,"end":440,"text":"menos dos","role":"data","reading":"natural"},{"start":443,"end":453,"text":"ciento dos","role":"data","reading":"natural"},{"start":515,"end":535,"text":"seis con veinticinco","role":"data","reading":"natural"}]}}''')
UNSOLVED={(3,5),(6,5),(6,16)}
OPTIONAL_CASES={(4,7):6,(6,16):6,(7,8):6}

def code_slot(block):
    return "initialCode" if block["type"]=="cpp_compiler" else "code"

class SquareRootContractFormatBatch(unittest.TestCase):
    def setUp(self):
        self.doc=json.loads(PATH.read_text(encoding="utf-8"))
        self.meta=self.doc["stateLayout"]
        self.originals={x["id"]:x["block"] for x in self.meta["sourceFormatRevisions"]}

    def test_state_provenance_and_reading_overlays(self):
        state.validate(self.doc)
        self.assertEqual(16,len(POSITIONS))
        self.assertEqual(16,len(self.originals))
        self.assertEqual(
            [self.meta["current"][p]["blocks"][b] for p,b in POSITIONS],
            [x["id"] for x in self.meta["editorial"][-19:-3]])
        self.assertEqual(
            [self.meta["current"][p]["blocks"][b] for p,b in PROSE],
            [x["id"] for x in self.meta["editorial"][-3:]])
        prior=deepcopy(self.doc)
        prior["stateLayout"].pop("sourceFormatRevisions")
        prior["stateLayout"]["editorial"]=prior["stateLayout"]["editorial"][:-19]
        for p,b in POSITIONS:
            identity=self.meta["current"][p]["blocks"][b]
            old,new=self.originals[identity],self.doc["pages"][p]["blocks"][b]
            self.assertTrue(require_source_format(old,new),identity)
            slot=code_slot(old)
            self.assertEqual(
                [line for line in old[slot].splitlines() if line.strip()],
                [line for line in new[slot].splitlines() if line.strip()],identity)
            self.assertGreater(new[slot].count("\n\n"),old[slot].count("\n\n"),identity)
            self.assertEqual(old.get("outputCheck"),new.get("outputCheck"),identity)
            self.assertEqual(old.get("answers"),new.get("answers"),identity)
            self.assertEqual(old.get("compileCheck"),new.get("compileCheck"),identity)
            prior["pages"][p]["blocks"][b]=deepcopy(old)
        for p,b in PROSE:
            identity=self.meta["current"][p]["blocks"][b]
            prior["pages"][p]["blocks"][b]["text"]=OLD_PARAGRAPHS[identity]
            semantic=next(x for x in prior["textSemantics"]["fields"]
                          if x["block"]==identity and x["path"]=="/text")
            semantic.clear()
            semantic.update(deepcopy(OLD_SEMANTICS[identity]))
        state.validate(prior)
        state.validate_transition(prior,self.doc)
        self.assertEqual(state.validate(prior),state.validate(self.doc))
        for key in ("origin","current","family","additions"):
            self.assertEqual(prior["stateLayout"][key],self.meta[key])
        for p,b in PROSE:
            self.assertIn("primer",self.doc["pages"][p]["blocks"][b]["text"])
            self.assertIn("opcional",self.doc["pages"][p]["blocks"][b]["text"])
        self.assertEqual("const bool finita = std::___(area);",
                         self.doc["pages"][1]["blocks"][6]["code"])
        self.assertTrue(self.doc["compilerPuzzlesRequired"])

    def test_cpp23_identical_outputs_and_all_declared_cases(self):
        with tempfile.TemporaryDirectory(prefix="u6l9-sqrt-format-") as folder:
            for i,(p,b) in enumerate(POSITIONS):
                identity=self.meta["current"][p]["blocks"][b]
                old,new=self.originals[identity],self.doc["pages"][p]["blocks"][b]
                cases=new.get("outputCheck",{}).get("cases",[])
                inputs=[case["standardInput"] for case in cases] or [""]
                historical=[]
                for label,block in (("before",old),("formatted",new)):
                    source=Path(folder)/f"{i}-{label}.cpp"
                    binary=Path(folder)/f"{i}-{label}"
                    source.write_text(block[code_slot(block)],encoding="utf-8")
                    compiled=subprocess.run(["c++","-std=c++23","-O0",str(source),"-o",str(binary)],
                                            capture_output=True,text=True,timeout=90)
                    self.assertEqual(0,compiled.returncode,identity+compiled.stderr)
                    outputs=[]
                    for data in inputs:
                        execution=subprocess.run([str(binary)],input=data,
                                                 capture_output=True,text=True,timeout=15)
                        self.assertEqual(0,execution.returncode,identity+execution.stderr)
                        self.assertEqual("",execution.stderr,identity)
                        outputs.append(execution.stdout)
                    historical.append(outputs)
                self.assertEqual(historical[0],historical[1],identity)
                if new["type"]=="output_prediction":
                    self.assertIn(historical[1][0].rstrip("\n"),new["answers"],identity)
                if cases:
                    for actual,case in zip(historical[1],cases):
                        if (p,b) in UNSOLVED:
                            self.assertNotIn(actual,case["outputs"],identity)
                        else:
                            self.assertIn(actual,case["outputs"],identity)
                if (p,b) in OPTIONAL_CASES:
                    self.assertEqual(OPTIONAL_CASES[(p,b)],len(cases))
        owned=self.doc["pages"][6]["blocks"][16]
        self.assertEqual(6,len(owned["outputCheck"]["cases"]))
        self.assertEqual("0",owned["outputCheck"]["cases"][0]["standardInput"])
        self.assertEqual("#include <iostream>\n#include <cmath>\n#include <vector>\n\n"
                         "// Define calcular_lado antes de main, o declara su firma y define despues.\n"
                         "int main() {\n"
                         "    // Construye la lectura, la clasificacion y la comprobacion de conservacion.\n"
                         "    return 0;\n}\n",owned["initialCode"])

if __name__=="__main__":
    unittest.main()
