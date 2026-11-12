"""U6L11: reproducible C++23 round/truncate behavior and historic state."""
from copy import deepcopy
import json
from pathlib import Path
import subprocess
import tempfile
import unittest

import stable_state_layout as state
from code_format_compat import require_source_format

ROOT=Path(__file__).resolve().parents[2]
PATH=ROOT/"app/src/main/assets/content/es/programming/cpp_fundamentals/funciones_y_contratos/lesson_redondeo_deliberado_y_conversion_segura.json"
POSITIONS=((1,2),(1,4),(2,5),(3,2),(3,9),(4,2),(4,3),(5,5),(5,17),(6,2),(6,9),(7,1),(7,4))
PROSE=((3,8),(5,16),(6,8))
OLD_TEXT=json.loads(r'''{"block_0003_0008":"El programa lee origen finito y destino entero previo. Comprueba `0.5 37`, `-0.5 37`, `99.5 37`, `-99.5 37`, `100.0 37`, `100.25 37` y `-100.25 -8`. La salida muestra bool y destino, seguido de origen y conservación. Un rechazo debe mantener el destino que se entregó.\n\nPulsa «Compilar, ejecutar y comprobar». Conserva el programa al variar sólo Entrada entre los casos declarados. El salto después del último mensaje es opcional.","marcas_redondeadas_construccion_multicaso_comprobacion":"Escribe también la lectura de Entrada, las llamadas y la presentación. Pulsa «Compilar, ejecutar y comprobar» y conserva las mismas funciones y el mismo recorrido al cambiar Entrada entre los seis casos. Si un caso difiere, comprueba primero qué observaciones se aceptan, qué marcas ya estaban presentes y qué elementos deben conservarse.","block_0006_0008":"El programa lee primero una cantidad no negativa y después ese número de valores escritos válidamente. Algunos casos incumplen deliberadamente el dominio de la función y deben rechazarse antes del cálculo. El bucle de preparación construye la colección; las funciones conservan sus contratos. Comprueba estas entradas: `0`; `1 0.0`; `2 101.0 -101.0`; `2 0.25 -0.25`; `3 3.0 3.0 3.0`; `4 -100.0 100.0 99.5 -99.5`. El dominio de preparación conserva hasta ocho valores finitos de menos ciento dos a ciento dos.\n\nPulsa «Compilar, ejecutar y comprobar». Conserva el programa al variar sólo Entrada entre los casos declarados. El salto después del último mensaje es opcional."}''')
OLD_FIELDS=json.loads(r'''[{"block":"block_0003_0008","path":"/text","sourceSha256":"f1b11287fc04f1705c3ea33db19bb109a6781f01401fc0bec5095d21ada4c542","spans":[{"start":16,"end":22,"text":"origen","role":"cpp","reading":"technical"},{"start":32,"end":39,"text":"destino","role":"cpp","reading":"technical"},{"start":66,"end":72,"text":"0.5 37","role":"data","reading":"natural"},{"start":76,"end":83,"text":"-0.5 37","role":"data","reading":"natural"},{"start":87,"end":94,"text":"99.5 37","role":"data","reading":"natural"},{"start":98,"end":106,"text":"-99.5 37","role":"data","reading":"natural"},{"start":110,"end":118,"text":"100.0 37","role":"data","reading":"natural"},{"start":122,"end":131,"text":"100.25 37","role":"data","reading":"natural"},{"start":136,"end":146,"text":"-100.25 -8","role":"data","reading":"natural"},{"start":167,"end":171,"text":"bool","role":"cpp","reading":"technical"},{"start":174,"end":181,"text":"destino","role":"cpp","reading":"technical"},{"start":194,"end":200,"text":"origen","role":"cpp","reading":"technical"}]},{"block":"marcas_redondeadas_construccion_multicaso_comprobacion","path":"/text","sourceSha256":"ca052b3835de81d7a094088e95f290dae293127a948698c729e59f0acad0d9cc","spans":[{"start":192,"end":196,"text":"seis","role":"data","reading":"natural"}]},{"block":"block_0006_0008","path":"/text","sourceSha256":"ab091497f0935bbb2cd5b7b6da5073ae0cd76b65024e30f1dd0bacd455e9cc14","spans":[{"start":320,"end":321,"text":"0","role":"data","reading":"natural"},{"start":325,"end":330,"text":"1 0.0","role":"data","reading":"natural"},{"start":334,"end":348,"text":"2 101.0 -101.0","role":"data","reading":"natural"},{"start":352,"end":364,"text":"2 0.25 -0.25","role":"data","reading":"natural"},{"start":368,"end":381,"text":"3 3.0 3.0 3.0","role":"data","reading":"natural"},{"start":385,"end":410,"text":"4 -100.0 100.0 99.5 -99.5","role":"data","reading":"natural"},{"start":454,"end":458,"text":"ocho","role":"data","reading":"natural"},{"start":478,"end":494,"text":"menos ciento dos","role":"data","reading":"natural"},{"start":497,"end":507,"text":"ciento dos","role":"data","reading":"natural"}]}]''')
UNSOLVED={(2,5),(5,5),(5,17)}

def slot(block):
    return "initialCode" if block["type"]=="cpp_compiler" else "code"

class RoundingSourceFormatBatch(unittest.TestCase):
    def setUp(self):
        self.doc=json.loads(PATH.read_text(encoding="utf-8"))
        self.meta=self.doc["stateLayout"]
        self.original={item["id"]:item["block"] for item in self.meta["sourceFormatRevisions"]}

    def test_historical_layout_and_reading_overlays(self):
        state.validate(self.doc)
        self.assertEqual(13,len(POSITIONS))
        self.assertEqual(13,len(self.original))
        self.assertEqual([self.meta["current"][p]["blocks"][b] for p,b in POSITIONS],
                         [e["id"] for e in self.meta["editorial"][-16:-3]])
        self.assertEqual([self.meta["current"][p]["blocks"][b] for p,b in PROSE],
                         [e["id"] for e in self.meta["editorial"][-3:]])
        previous=deepcopy(self.doc)
        previous["stateLayout"].pop("sourceFormatRevisions")
        previous["stateLayout"]["editorial"]=previous["stateLayout"]["editorial"][:-16]
        for p,b in POSITIONS:
            ident=self.meta["current"][p]["blocks"][b]
            old,new=self.original[ident],self.doc["pages"][p]["blocks"][b]
            self.assertTrue(require_source_format(old,new),ident)
            key=slot(old)
            self.assertEqual([s for s in old[key].splitlines() if s.strip()],
                             [s for s in new[key].splitlines() if s.strip()],ident)
            self.assertGreater(new[key].count("\n\n"),old[key].count("\n\n"),ident)
            for name in ("answers","outputCheck","compileCheck"):
                self.assertEqual(old.get(name),new.get(name),ident)
            previous["pages"][p]["blocks"][b]=deepcopy(old)
        for p,b in PROSE:
            ident=self.meta["current"][p]["blocks"][b]
            self.assertIn("primer caso",self.doc["pages"][p]["blocks"][b]["text"])
            self.assertIn("pruebas opcionales",self.doc["pages"][p]["blocks"][b]["text"])
            previous["pages"][p]["blocks"][b]["text"]=OLD_TEXT[ident]
            descriptor=next(field for field in previous["textSemantics"]["fields"]
                            if field["block"]==ident and field["path"]=="/text")
            descriptor.clear()
            descriptor.update(deepcopy(next(v for v in OLD_FIELDS if v["block"]==ident)))
        state.validate(previous)
        state.validate_transition(previous,self.doc)
        self.assertEqual(state.validate(previous),state.validate(self.doc))
        for key in ("origin","current","family","additions"):
            self.assertEqual(previous["stateLayout"][key],self.meta[key])

    def test_cpp23_before_and_after_outputs_and_all_optional_cases(self):
        with tempfile.TemporaryDirectory(prefix="u6l11-rounding-") as folder:
            for i,(p,b) in enumerate(POSITIONS):
                ident=self.meta["current"][p]["blocks"][b]
                old,new=self.original[ident],self.doc["pages"][p]["blocks"][b]
                cases=new.get("outputCheck",{}).get("cases",[])
                inputs=[c["standardInput"] for c in cases] or [""]
                results=[]
                for label,block in (("old",old),("new",new)):
                    source=Path(folder)/f"{i}-{label}.cpp"
                    executable=Path(folder)/f"{i}-{label}"
                    source.write_text(block[slot(block)],encoding="utf-8")
                    compiled=subprocess.run(["c++","-std=c++23","-O0",str(source),"-o",str(executable)],
                                            capture_output=True,text=True,timeout=90)
                    self.assertEqual(0,compiled.returncode,ident+compiled.stderr)
                    runs=[]
                    for data in inputs:
                        run=subprocess.run([str(executable)],input=data,capture_output=True,text=True,timeout=15)
                        self.assertEqual(0,run.returncode,ident+run.stderr)
                        self.assertEqual("",run.stderr,ident)
                        runs.append(run.stdout)
                    results.append(runs)
                self.assertEqual(results[0],results[1],ident)
                if new["type"]=="output_prediction":
                    self.assertIn(results[1][0].rstrip("\n"),new["answers"],ident)
                for output,case in zip(results[1],cases):
                    if (p,b) in UNSOLVED:
                        self.assertNotIn(output,case["outputs"],ident)
                    else:
                        self.assertIn(output,case["outputs"],ident)
        self.assertEqual(7,len(self.doc["pages"][3]["blocks"][9]["outputCheck"]["cases"]))
        self.assertEqual(6,len(self.doc["pages"][5]["blocks"][17]["outputCheck"]["cases"]))
        self.assertEqual(6,len(self.doc["pages"][6]["blocks"][9]["outputCheck"]["cases"]))

    def test_one_mandatory_case_all_others_documented(self):
        for p,b,cp,cb in ((3,8,3,9),(5,16,5,17),(6,8,6,9)):
            paragraph=self.doc["pages"][p]["blocks"][b]["text"]
            cases=self.doc["pages"][cp]["blocks"][cb]["outputCheck"]["cases"]
            self.assertIn("primer caso",paragraph)
            self.assertIn("no bloquean el paso",paragraph)
            self.assertIn(cases[0]["standardInput"],paragraph)
            if p==5:
                descriptions="\n".join(self.doc["pages"][5]["blocks"][i]["text"] for i in range(10,16))
                for case in cases:
                    self.assertIn("Entrada: `"+case["standardInput"]+"`",descriptions)
                    self.assertIn("`"+case["outputs"][0].split("\n")[0]+"`",descriptions)
            else:
                for case in cases:
                    self.assertIn("Entrada «"+case["standardInput"]+"». Salida:",paragraph)
                    self.assertIn(case["outputs"][0],paragraph)
        for p,b in UNSOLVED:
            self.assertIn("Construye",self.doc["pages"][p]["blocks"][b]["initialCode"])

if __name__=="__main__":
    unittest.main()
