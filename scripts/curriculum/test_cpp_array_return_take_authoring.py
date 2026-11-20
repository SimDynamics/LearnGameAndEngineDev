"""Read-only Spanish v2 authoring transition for the original U6L12 editor.

Only a root declaration is added in memory. No public case, original
student source, block address, signature, draft or historical achievement is
rewritten or attributed to the new semantic goal.
"""
from copy import deepcopy
import json
from pathlib import Path
import unittest

import stable_state_layout as state

ROOT=Path(__file__).resolve().parents[2]
LESSON=ROOT/("app/src/main/assets/content/es/programming/"
             "cpp_language_memory_oop/estado_y_ciclo_de_vida_de_clases/"
             "lesson_recibir_y_devolver_objetos_propietarios.json")
OWNER="block_0021_0002"
FIELD="compilerArrayReturnTakeChecks"
SPEC={
    "activity":OWNER,
    "className":"Tramo",
    "ownerMember":"datos_",
    "countMember":"cantidad_",
    "copyFunction":"copia_modificada",
    "takeFunction":"tomar_si_cabe",
    "sourceParameter":"origen",
    "indexParameter":"indice",
    "valueParameter":"valor",
    "destinationParameter":"destino",
    "offeredParameter":"origen",
    "limitParameter":"limite",
    "quantityMethod":"cantidad",
    "accessMethod":"acceso",
    "configureMethod":"configurar",
    "readMethod":"leer",
    "writeMethod":"escribir",
    "clearMethod":"vaciar",
}

def pair():
    published=json.loads(LESSON.read_text(encoding="utf-8"))
    amended=deepcopy(published)
    amended[FIELD]=[deepcopy(SPEC)]
    return published,amended


class ArrayReturnTakeAuthoring(unittest.TestCase):
    def test_only_original_owner_root_addition(self):
        before,after=pair()
        self.assertNotIn(FIELD,before)
        self.assertEqual(state.validate(before),state.validate(after))
        state.validate_transition(before,after)
        self.assertEqual(before["pages"],after["pages"])
        self.assertEqual(before["stateLayout"],after["stateLayout"])
        previous=state.original_projection(before)
        revised=state.original_projection(after)
        revised.pop(FIELD)
        self.assertEqual(previous,revised)

    def test_rejects_forged_owners_and_unknown_contract_symbols(self):
        cases=("duplicate","missing-owner","unknown-field","missing-field",
               "wrong-class","wrong-array","wrong-count","wrong-copy-name",
               "wrong-take-name","wrong-source","wrong-take-source",
               "invalid-method","reserved-name","overlapping-private",
               "wrong-layout","missing-outputs")
        for name in cases:
            with self.subTest(name=name):
                published,after=pair()
                spec=after[FIELD][0]
                if name=="duplicate":after[FIELD].append(deepcopy(spec))
                elif name=="missing-owner":spec["activity"]="block_9999_9999"
                elif name=="unknown-field":spec["fabricatedProof"]=True
                elif name=="missing-field":del spec["limitParameter"]
                elif name=="wrong-class":spec["className"]="Otro"
                elif name=="wrong-array":spec["ownerMember"]="inventado"
                elif name=="wrong-count":spec["countMember"]="inventado"
                elif name=="wrong-copy-name":spec["copyFunction"]="otra_copia"
                elif name=="wrong-take-name":spec["takeFunction"]="recibir"
                elif name=="wrong-source":spec["sourceParameter"]="otra"
                elif name=="wrong-take-source":spec["offeredParameter"]="otra"
                elif name=="invalid-method":spec["writeMethod"]="escribir();std::exit(0)"
                elif name=="reserved-name":spec["className"]="__Tramo"
                elif name=="overlapping-private":
                    after["compilerArrayCopyMoveChecks"]=[
                        {"activity":OWNER,"className":"Tramo"}]
                elif name=="wrong-layout":after["stateLayout"]["version"]=1
                else:
                    locations=[(p,b) for p,page in
                               enumerate(after["stateLayout"]["current"])
                               for b,id in enumerate(page["blocks"]) if id==OWNER]
                    self.assertEqual(len(locations),1)
                    p,b=locations[0]
                    after["pages"][p]["blocks"][b]["outputCheck"]["cases"]=[]
                with self.assertRaises((ValueError,KeyError,TypeError)):
                    state.validate(after)
                self.assertNotIn(FIELD,published)

    def test_cannot_rewrite_original_source_while_declaring_new_goal(self):
        before,after=pair()
        locations=[(p,b) for p,page in enumerate(after["stateLayout"]["current"])
                   for b,id in enumerate(page["blocks"]) if id==OWNER]
        self.assertEqual(len(locations),1)
        p,b=locations[0]
        after["pages"][p]["blocks"][b]["initialCode"] += (
            "\n// This published program must remain unchanged.\n")
        with self.assertRaises((ValueError,KeyError,TypeError)):
            state.validate_transition(before,after)

if __name__=="__main__":
    unittest.main(verbosity=2)
