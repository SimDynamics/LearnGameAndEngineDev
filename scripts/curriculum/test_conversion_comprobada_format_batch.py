"""U6L10: preserve historic source, optional cases, and numeric conversion output."""
from copy import deepcopy
import json
from pathlib import Path
import subprocess
import tempfile
import unittest

import stable_state_layout as state
from code_format_compat import require_source_format

ROOT=Path(__file__).resolve().parents[2]
LESSON=ROOT/"app/src/main/assets/content/es/programming/cpp_fundamentals/funciones_y_contratos/lesson_conversion_comprobada_sin_perdida.json"
POSITIONS=((0,3),(1,3),(1,4),(2,5),(3,2),(3,7),(4,2),(4,4),(5,5),(6,2),(6,4),(6,9),(6,12))
PROSE=((3,6),(6,8),(6,11))
OLD_TEXT=json.loads(r'''{"block_0003_0006":"El programa lee un origen finito y un destino entero previo. Comprueba estas parejas: `-100.0 42`, `-100.5 42`, `1.0 42`, `0.5 42`, `-0.5 42`, `99.75 42` y `6.5 -8`. Se muestran aceptación, destino posterior, origen y si el origen se conserva. Un rechazo debe conservar el destino recibido y no una constante interna.\n\nPulsa «Compilar, ejecutar y comprobar». Conserva el programa al variar sólo Entrada entre los casos declarados. El salto después del último mensaje es opcional.","block_0006_0008":"Construye desde el esqueleto `convertir_entero` y un cliente que lea primero la cantidad de valores y después esos `double`. Usa como máximo ocho valores finitos de menos ciento dos a ciento dos. Conserva una copia del vector, prepara destino diecisiete para cada llamada y presenta las cuatro líneas del informe: `Enteros:`, `Rechazados: N`, `Ceros aceptados: N` y `Datos conservados: B`. Cada entero aceptado termina en punto y coma.\n\nComprueba `0`; `1 0.0`; `1 0.5`; `1 -0.5`; `2 -100.0 100.0`; `2 -100.5 100.5`; y `4 0.0 0.5 0.0 -0.5`. Pulsa «Compilar, ejecutar y comprobar» conservando las funciones entre casos; el salto final es opcional.","block_0006_0011":"Este programa de referencia lee cantidad, valores y destino previo para cada llamada. Comprueba `4 0.0 0.5 0.0 -0.5 17` y la misma colección con destino `-8`. Ambos informes deben ser `Enteros:0;0;`, dos rechazos, dos ceros aceptados y conservación verdadera. El bool decide la pertenencia de cada resultado.\n\nPulsa «Compilar, ejecutar y comprobar». Conserva el programa al variar sólo Entrada entre los casos declarados. El salto después del último mensaje es opcional."}''')
OLD_DESCRIPTORS=json.loads(r'''{"block_0003_0006":{"block":"block_0003_0006","path":"/text","sourceSha256":"5918f7669d99134b08925ba97eb301e3da2b59fa57e986e35ee3b04e6553b3b8","spans":[{"start":87,"end":96,"text":"-100.0 42","role":"data","reading":"natural"},{"start":100,"end":109,"text":"-100.5 42","role":"data","reading":"natural"},{"start":113,"end":119,"text":"1.0 42","role":"data","reading":"natural"},{"start":123,"end":129,"text":"0.5 42","role":"data","reading":"natural"},{"start":133,"end":140,"text":"-0.5 42","role":"data","reading":"natural"},{"start":144,"end":152,"text":"99.75 42","role":"data","reading":"natural"},{"start":157,"end":163,"text":"6.5 -8","role":"data","reading":"natural"}]},"block_0006_0008":{"block":"block_0006_0008","path":"/text","sourceSha256":"979755c6eca1941a98a82bd89e5ab11d907c3974491d4a11cf048e1a574df947","spans":[{"start":30,"end":46,"text":"convertir_entero","role":"cpp","reading":"technical"},{"start":116,"end":122,"text":"double","role":"cpp","reading":"technical"},{"start":141,"end":145,"text":"ocho","role":"data","reading":"natural"},{"start":165,"end":181,"text":"menos ciento dos","role":"data","reading":"natural"},{"start":184,"end":194,"text":"ciento dos","role":"data","reading":"natural"},{"start":243,"end":253,"text":"diecisiete","role":"data","reading":"natural"},{"start":287,"end":293,"text":"cuatro","role":"data","reading":"natural"},{"start":315,"end":323,"text":"Enteros:","role":"output","reading":"natural"},{"start":327,"end":340,"text":"Rechazados: N","role":"output","reading":"natural"},{"start":344,"end":362,"text":"Ceros aceptados: N","role":"output","reading":"natural"},{"start":367,"end":387,"text":"Datos conservados: B","role":"output","reading":"natural"},{"start":448,"end":449,"text":"0","role":"data","reading":"natural"},{"start":453,"end":458,"text":"1 0.0","role":"data","reading":"natural"},{"start":462,"end":467,"text":"1 0.5","role":"data","reading":"natural"},{"start":471,"end":477,"text":"1 -0.5","role":"data","reading":"natural"},{"start":481,"end":495,"text":"2 -100.0 100.0","role":"data","reading":"natural"},{"start":499,"end":513,"text":"2 -100.5 100.5","role":"data","reading":"natural"},{"start":519,"end":537,"text":"4 0.0 0.5 0.0 -0.5","role":"data","reading":"natural"}]},"block_0006_0011":{"block":"block_0006_0011","path":"/text","sourceSha256":"55cbb8f92db8f9970faec80e2e71fa1094b04175a868afd418fe7e18f04cf874","spans":[{"start":97,"end":118,"text":"4 0.0 0.5 0.0 -0.5 17","role":"data","reading":"natural"},{"start":154,"end":156,"text":"-8","role":"data","reading":"natural"},{"start":185,"end":197,"text":"Enteros:0;0;","role":"output","reading":"natural"},{"start":200,"end":203,"text":"dos","role":"output","reading":"natural"},{"start":214,"end":217,"text":"dos","role":"output","reading":"natural"},{"start":249,"end":258,"text":"verdadera","role":"output","reading":"natural"},{"start":263,"end":267,"text":"bool","role":"cpp","reading":"technical"}]}}''')
UNSOLVED={(2,5),(5,5),(6,9)}
CASE_COUNTS={(3,7):7,(6,9):7,(6,12):2}

def slot(block):
    return "initialCode" if block["type"]=="cpp_compiler" else "code"

class VerifiedConversionFormatBatch(unittest.TestCase):
    def setUp(self):
        self.doc=json.loads(LESSON.read_text(encoding="utf-8"))
        self.meta=self.doc["stateLayout"]
        self.old={x["id"]:x["block"] for x in self.meta["sourceFormatRevisions"]}

    def test_immutable_state_and_editorial_transitions(self):
        state.validate(self.doc)
        self.assertEqual(13,len(POSITIONS))
        self.assertEqual(13,len(self.old))
        self.assertEqual([self.meta["current"][p]["blocks"][b] for p,b in POSITIONS],
                         [x["id"] for x in self.meta["editorial"][-16:-3]])
        self.assertEqual([self.meta["current"][p]["blocks"][b] for p,b in PROSE],
                         [x["id"] for x in self.meta["editorial"][-3:]])
        previous=deepcopy(self.doc)
        previous["stateLayout"].pop("sourceFormatRevisions")
        previous["stateLayout"]["editorial"]=previous["stateLayout"]["editorial"][:-16]
        for p,b in POSITIONS:
            identity=self.meta["current"][p]["blocks"][b]
            old,new=self.old[identity],self.doc["pages"][p]["blocks"][b]
            self.assertTrue(require_source_format(old,new),identity)
            key=slot(old)
            self.assertEqual([v for v in old[key].splitlines() if v.strip()],
                             [v for v in new[key].splitlines() if v.strip()],identity)
            self.assertGreater(new[key].count("\n\n"),old[key].count("\n\n"),identity)
            for k in ("answers","outputCheck","compileCheck"):
                self.assertEqual(old.get(k),new.get(k),identity)
            previous["pages"][p]["blocks"][b]=deepcopy(old)
        for p,b in PROSE:
            identity=self.meta["current"][p]["blocks"][b]
            text=self.doc["pages"][p]["blocks"][b]["text"]
            self.assertIn("primera entrada",text)
            self.assertIn("pruebas opcionales",text)
            self.assertNotIn("\n- ",text)
            previous["pages"][p]["blocks"][b]["text"]=OLD_TEXT[identity]
            overlay=next(x for x in previous["textSemantics"]["fields"]
                         if x["block"]==identity and x["path"]=="/text")
            overlay.clear()
            overlay.update(deepcopy(OLD_DESCRIPTORS[identity]))
        state.validate(previous)
        state.validate_transition(previous,self.doc)
        self.assertEqual(state.validate(previous),state.validate(self.doc))
        for k in ("origin","family","current","additions"):
            self.assertEqual(previous["stateLayout"][k],self.meta[k])

    def test_cpp23_original_and_formatted_executions(self):
        with tempfile.TemporaryDirectory(prefix="u6l10-conversion-") as folder:
            for index,(p,b) in enumerate(POSITIONS):
                ident=self.meta["current"][p]["blocks"][b]
                before=self.old[ident]
                after=self.doc["pages"][p]["blocks"][b]
                cases=after.get("outputCheck",{}).get("cases",[])
                inputs=[c["standardInput"] for c in cases] or [""]
                history=[]
                for version,block in (("old",before),("new",after)):
                    source=Path(folder)/f"{index}-{version}.cpp"
                    exe=Path(folder)/f"{index}-{version}"
                    source.write_text(block[slot(block)],encoding="utf-8")
                    built=subprocess.run(["c++","-std=c++23","-O0",str(source),"-o",str(exe)],
                                         capture_output=True,text=True,timeout=90)
                    self.assertEqual(0,built.returncode,ident+built.stderr)
                    runs=[]
                    for inp in inputs:
                        run=subprocess.run([str(exe)],input=inp,capture_output=True,
                                           text=True,timeout=15)
                        self.assertEqual(0,run.returncode,ident+run.stderr)
                        self.assertEqual("",run.stderr,ident)
                        runs.append(run.stdout)
                    history.append(runs)
                self.assertEqual(history[0],history[1],ident)
                if after["type"]=="output_prediction":
                    self.assertIn(history[1][0].rstrip("\n"),after["answers"],ident)
                for output,case in zip(history[1],cases):
                    if (p,b) in UNSOLVED:
                        self.assertNotIn(output,case["outputs"],ident)
                    else:
                        self.assertIn(output,case["outputs"],ident)
                if (p,b) in CASE_COUNTS:
                    self.assertEqual(CASE_COUNTS[(p,b)],len(cases))

    def test_every_optional_input_and_output_are_documented(self):
        for (p,b),(cp,cb) in zip(PROSE,((3,7),(6,9),(6,12))):
            text=self.doc["pages"][p]["blocks"][b]["text"]
            cases=self.doc["pages"][cp]["blocks"][cb]["outputCheck"]["cases"]
            for case in cases:
                self.assertIn("Entrada «"+case["standardInput"]+"». Salida:",text)
                self.assertIn(case["outputs"][0],text)
        for p,b in UNSOLVED:
            self.assertIn("Construye",self.doc["pages"][p]["blocks"][b]["initialCode"])

if __name__=="__main__":
    unittest.main()
