"""U6L8: history-safe C++23 format, optional cases and prose-reading preservation."""
from copy import deepcopy
import json
from pathlib import Path
import subprocess
import tempfile
import unittest

import stable_state_layout as state
from code_format_compat import require_source_format

ROOT=Path(__file__).resolve().parents[2]
PATH=ROOT/"app/src/main/assets/content/es/programming/cpp_fundamentals/funciones_y_contratos/lesson_componer_funciones_pequenas.json"
POSITIONS=((0,2),(1,2),(2,2),(3,4),(4,1),(6,3),(6,7),(6,28))
PROSE_POSITIONS={"plan_reserva_construccion_entrada":(6,14),
                 "plan_reserva_casos_de_rechazo":(6,21)}
OLD_PARAGRAPHS=json.loads(r'''{"plan_reserva_construccion_entrada":"Completa el esqueleto con las cuatro funciones anteriores y main. La entrada contiene, en este orden, n, la cantidad de pedidos; exactamente n valores de pedido; capacidad; y reserva. Usa números enteros separados por espacios o saltos de línea. Los ensayos garantizan n entre 0 y 7, pedidos entre -1 y 21, capacidad entre 0 y 11 y reserva entre -1 y 4, con todos los datos presentes y escritos como enteros. Estos límites permiten leer y guardar el ensayo completo; no significan que el plan sea válido.\n\nEn main, lee los datos y construye el vector. Llama a plan_valido antes de calcular unidades o dividir. Si devuelve false, muestra únicamente Datos fuera del dominio y un salto de línea. Si devuelve true, compón los otros cálculos y pasa sus resultados a mostrar_plan. Conserva las funciones al variar sólo Entrada. Pulsa «Compilar, ejecutar y comprobar» para comprobar los catorce casos.","plan_reserva_casos_de_rechazo":"Estos cinco ensayos deben mostrar únicamente Datos fuera del dominio, con salto de línea final:\n- 1 3 0 1: la capacidad es 0.\n- 1 -1 5 0: un pedido es -1.\n- 7 1 1 1 1 1 1 1 5 0: hay 7 pedidos.\n- 0 5 4: la reserva es 4.\n- 0 11 0: la capacidad es 11.\nSe leen los datos de cada ensayo, pero no se calcula el total ni se divide cuando el plan no cumple el contrato."}''')
OLD_DESCRIPTORS=json.loads(r'''{"plan_reserva_construccion_entrada":{"block":"plan_reserva_construccion_entrada","path":"/text","sourceSha256":"01051efac7ed791fb60494e506ad073e24e24ff8ce62713a0a5366c75e9f8835","spans":[{"start":30,"end":36,"text":"cuatro","role":"data","reading":"natural"},{"start":60,"end":64,"text":"main","role":"cpp","reading":"technical"},{"start":102,"end":103,"text":"n","role":"cpp","reading":"technical"},{"start":141,"end":142,"text":"n","role":"cpp","reading":"technical"},{"start":162,"end":171,"text":"capacidad","role":"cpp","reading":"technical"},{"start":175,"end":182,"text":"reserva","role":"cpp","reading":"technical"},{"start":269,"end":270,"text":"n","role":"cpp","reading":"technical"},{"start":277,"end":278,"text":"0","role":"data","reading":"natural"},{"start":281,"end":282,"text":"7","role":"data","reading":"natural"},{"start":298,"end":300,"text":"-1","role":"data","reading":"natural"},{"start":303,"end":305,"text":"21","role":"data","reading":"natural"},{"start":307,"end":316,"text":"capacidad","role":"cpp","reading":"technical"},{"start":323,"end":324,"text":"0","role":"data","reading":"natural"},{"start":327,"end":329,"text":"11","role":"data","reading":"natural"},{"start":332,"end":339,"text":"reserva","role":"cpp","reading":"technical"},{"start":346,"end":348,"text":"-1","role":"data","reading":"natural"},{"start":351,"end":352,"text":"4","role":"data","reading":"natural"},{"start":509,"end":513,"text":"main","role":"cpp","reading":"technical"},{"start":560,"end":571,"text":"plan_valido","role":"cpp","reading":"technical"},{"start":622,"end":627,"text":"false","role":"output","reading":"natural"},{"start":648,"end":671,"text":"Datos fuera del dominio","role":"output","reading":"natural"},{"start":705,"end":709,"text":"true","role":"output","reading":"natural"},{"start":761,"end":773,"text":"mostrar_plan","role":"cpp","reading":"technical"},{"start":880,"end":887,"text":"catorce","role":"data","reading":"natural"}]},"plan_reserva_casos_de_rechazo":{"block":"plan_reserva_casos_de_rechazo","path":"/text","sourceSha256":"96a606b181ff5633a3afbd3c93452ca2aeecad43068f4492a0b5eb4c10423114","spans":[{"start":6,"end":11,"text":"cinco","role":"data","reading":"natural"},{"start":45,"end":68,"text":"Datos fuera del dominio","role":"output","reading":"natural"},{"start":98,"end":105,"text":"1 3 0 1","role":"data","reading":"natural"},{"start":123,"end":124,"text":"0","role":"data","reading":"natural"},{"start":128,"end":136,"text":"1 -1 5 0","role":"data","reading":"natural"},{"start":151,"end":153,"text":"-1","role":"data","reading":"natural"},{"start":157,"end":176,"text":"7 1 1 1 1 1 1 1 5 0","role":"data","reading":"natural"},{"start":182,"end":183,"text":"7","role":"data","reading":"natural"},{"start":195,"end":200,"text":"0 5 4","role":"data","reading":"natural"},{"start":216,"end":217,"text":"4","role":"data","reading":"natural"},{"start":221,"end":227,"text":"0 11 0","role":"data","reading":"natural"},{"start":245,"end":247,"text":"11","role":"data","reading":"natural"}]}}''')
FIRST_INPUT="0 5 0"

def field(block):
    if block["type"]=="cpp_compiler":
        return "initialCode"
    if block["type"]=="inline_fill_code":
        return "template"
    return "code"

def runnable(block):
    source=block[field(block)]
    if block["type"]=="inline_fill_code":
        for answer in block["answers"]:
            source=source.replace("[]",answer,1)
    return source

class CompositionFunctionsFormatBatch(unittest.TestCase):
    def setUp(self):
        self.doc=json.loads(PATH.read_text(encoding="utf-8"))
        self.meta=self.doc["stateLayout"]
        self.original={e["id"]:e["block"] for e in self.meta["sourceFormatRevisions"]}

    def test_original_state_and_optional_first_case_wording(self):
        state.validate(self.doc)
        self.assertEqual(8,len(POSITIONS))
        self.assertEqual(8,len(self.original))
        self.assertEqual(
            [self.meta["current"][p]["blocks"][b] for p,b in POSITIONS],
            [e["id"] for e in self.meta["editorial"][-10:-2]])
        self.assertEqual(list(PROSE_POSITIONS),
                         [e["id"] for e in self.meta["editorial"][-2:]])
        previous=deepcopy(self.doc)
        previous["stateLayout"].pop("sourceFormatRevisions")
        previous["stateLayout"]["editorial"]=previous["stateLayout"]["editorial"][:-10]
        for p,b in POSITIONS:
            identity=self.meta["current"][p]["blocks"][b]
            old,new=self.original[identity],self.doc["pages"][p]["blocks"][b]
            self.assertTrue(require_source_format(old,new),identity)
            name=field(old)
            self.assertEqual(
                [line for line in old[name].splitlines() if line.strip()],
                [line for line in new[name].splitlines() if line.strip()],identity)
            self.assertGreater(new[name].count("\n\n"),old[name].count("\n\n"),identity)
            self.assertEqual(old.get("outputCheck"),new.get("outputCheck"),identity)
            self.assertEqual(old.get("answers"),new.get("answers"),identity)
            previous["pages"][p]["blocks"][b]=deepcopy(old)
        for identity,(p,b) in PROSE_POSITIONS.items():
            previous["pages"][p]["blocks"][b]["text"]=OLD_PARAGRAPHS[identity]
            descriptor=next(v for v in previous["textSemantics"]["fields"]
                            if v["block"]==identity and v["path"]=="/text")
            descriptor.clear()
            descriptor.update(deepcopy(OLD_DESCRIPTORS[identity]))
        state.validate(previous)
        state.validate_transition(previous,self.doc)
        self.assertEqual(state.validate(previous),state.validate(self.doc))
        for f in ("origin","family","current","additions"):
            self.assertEqual(previous["stateLayout"][f],self.meta[f])
        text=self.doc["pages"][6]["blocks"][14]["text"]
        self.assertIn("el primero de los catorce casos",text)
        self.assertIn("pruebas opcionales y no bloquean el avance",text)
        self.assertIn(FIRST_INPUT,text)
        rejection=self.doc["pages"][6]["blocks"][21]["text"]
        self.assertNotIn("\n- ",rejection)
        self.assertEqual(5,len([x for x in rejection.splitlines() if x.strip().startswith(("1 ","7 ","0 "))]))
        self.assertTrue(self.doc["compilerPuzzlesRequired"])

    def test_cpp23_before_after_and_all_declared_cases(self):
        with tempfile.TemporaryDirectory(prefix="u6l8-composition-") as folder:
            for n,(p,b) in enumerate(POSITIONS):
                identity=self.meta["current"][p]["blocks"][b]
                old,new=self.original[identity],self.doc["pages"][p]["blocks"][b]
                cases=new.get("outputCheck",{}).get("cases",[])
                inputs=[case["standardInput"] for case in cases] or [""]
                histories=[]
                for label,block in (("old",old),("formatted",new)):
                    source=Path(folder)/f"{n}-{label}.cpp"
                    binary=Path(folder)/f"{n}-{label}"
                    source.write_text(runnable(block),encoding="utf-8")
                    built=subprocess.run(["c++","-std=c++23","-O0",str(source),"-o",str(binary)],
                                         capture_output=True,text=True,timeout=90)
                    self.assertEqual(0,built.returncode,identity+built.stderr)
                    runs=[]
                    for data in inputs:
                        run=subprocess.run([str(binary)],input=data,capture_output=True,
                                           text=True,timeout=15)
                        self.assertEqual(0,run.returncode,identity+run.stderr)
                        self.assertEqual("",run.stderr,identity)
                        runs.append(run.stdout)
                    histories.append(runs)
                self.assertEqual(histories[0],histories[1],identity)
                if new["type"]=="output_prediction":
                    self.assertIn(histories[1][0].rstrip("\n"),new["answers"],identity)
                if new["type"]=="inline_fill_code":
                    self.assertEqual(["sumar",">="],new["answers"],identity)
                    self.assertEqual("1",histories[1][0],identity)
                for actual,case in zip(histories[1],cases):
                    self.assertIn(actual,case["outputs"],identity)

    def test_final_reference_satisfies_fourteen_optional_cases_without_solving_student_code(self):
        starter=self.doc["pages"][6]["blocks"][25]
        reference=self.doc["pages"][6]["blocks"][28]
        self.assertEqual("cpp_compiler",starter["type"])
        self.assertEqual(14,len(starter["outputCheck"]["cases"]))
        self.assertEqual("0 5 0",starter["outputCheck"]["cases"][0]["standardInput"])
        self.assertEqual("#include <iostream>\n#include <vector>\n\nint main() {\n    return 0;\n}\n",
                         starter["initialCode"])
        with tempfile.TemporaryDirectory(prefix="u6l8-reservation-reference-") as folder:
            source=Path(folder)/"reference.cpp"
            binary=Path(folder)/"reference"
            source.write_text(reference["code"],encoding="utf-8")
            built=subprocess.run(["c++","-std=c++23","-O0",str(source),"-o",str(binary)],
                                 capture_output=True,text=True,timeout=90)
            self.assertEqual(0,built.returncode,built.stderr)
            for i,case in enumerate(starter["outputCheck"]["cases"]):
                with self.subTest(index=i,stdin=case["standardInput"]):
                    run=subprocess.run([str(binary)],input=case["standardInput"],
                                       capture_output=True,text=True,timeout=15)
                    self.assertEqual(0,run.returncode,run.stderr)
                    self.assertEqual("",run.stderr)
                    self.assertIn(run.stdout,case["outputs"])

if __name__=="__main__":
    unittest.main()
