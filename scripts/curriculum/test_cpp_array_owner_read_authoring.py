"""Read-only authoring and historic identity checks for P3 U6L10.

Only a new optional lesson-root metadata declaration is introduced in-memory.
The published Lote editor, its first-case rule, sources, answer cases and state
addresses remain byte-for-byte unchanged.
"""
from copy import deepcopy
import json
from pathlib import Path
import unittest

import stable_state_layout as state

ROOT=Path(__file__).resolve().parents[2]
LESSON=ROOT/("app/src/main/assets/content/es/programming/cpp_language_memory_oop/"
             "estado_y_ciclo_de_vida_de_clases/"
             "lesson_gestionar_arrays_con_unique_ptr_y_cantidad.json")
OWNER="block_0017_0002"
FIELD="compilerArrayOwnerReadChecks"
SPEC={
    "activity":OWNER,
    "className":"Lote",
    "ownerMember":"datos_",
    "countMember":"cantidad_",
    "quantityParameter":"cantidad",
    "indexParameter":"indice",
    "outputParameter":"resultado",
    "configureMethod":"configurar",
    "readMethod":"leer",
    "quantityMethod":"cantidad",
    "accessMethod":"acceso",
    "writeMethod":"escribir",
    "clearMethod":"vaciar",
    "sumMethod":"sumar",
}

def pair():
    previous=json.loads(LESSON.read_text(encoding="utf-8"))
    revised=deepcopy(previous)
    revised[FIELD]=[deepcopy(SPEC)]
    return previous,revised

class ArrayOwnerReadAuthoring(unittest.TestCase):
    def test_root_addition_preserves_all_old_editor_and_response_contracts(self):
        previous,revised=pair()
        self.assertNotIn(FIELD,previous)
        self.assertEqual(state.validate(previous),state.validate(revised))
        state.validate_transition(previous,revised)
        self.assertEqual(previous["pages"],revised["pages"])
        self.assertEqual(previous["stateLayout"],revised["stateLayout"])
        previous_projection=state.original_projection(previous)
        future_projection=state.original_projection(revised)
        future_projection.pop(FIELD)
        self.assertEqual(previous_projection,future_projection)

    def test_untrusted_owner_policy_or_invalid_contracts_are_rejected(self):
        cases=("duplicate","missing-owner","unexpected-field","missing-field",
               "changed-class","changed-array","changed-quantity",
               "wrong-configure","wrong-read","wrong-parameter",
               "invalid-symbol","reserved-symbol","other-private-proof",
               "missing-cases","wrong-layout")
        for name in cases:
            with self.subTest(name=name):
                original,revised=pair()
                entry=revised[FIELD][0]
                if name=="duplicate":revised[FIELD].append(deepcopy(entry))
                elif name=="missing-owner":entry["activity"]="block_9999_9999"
                elif name=="unexpected-field":entry["forcedResult"]="true"
                elif name=="missing-field":entry.pop("readMethod")
                elif name=="changed-class":entry["className"]="Otra"
                elif name=="changed-array":entry["ownerMember"]="array_falso"
                elif name=="changed-quantity":entry["countMember"]="cantidad_falsa"
                elif name=="wrong-configure":entry["configureMethod"]="borrar"
                elif name=="wrong-read":entry["readMethod"]="sumar"
                elif name=="wrong-parameter":entry["indexParameter"]="n"
                elif name=="invalid-symbol":entry["writeMethod"]="escribir();exit(0)"
                elif name=="reserved-symbol":entry["className"]="__Lote"
                elif name=="other-private-proof":
                    revised["compilerMoveAssignmentInvariantChecks"]=[
                        {"activity":OWNER,"className":"Lote"}]
                elif name=="missing-cases":
                    found=[(p,k) for p,page in
                           enumerate(revised["stateLayout"]["current"])
                           for k,identity in enumerate(page["blocks"])
                           if identity==OWNER]
                    self.assertEqual(len(found),1)
                    p,k=found[0]
                    revised["pages"][p]["blocks"][k]["outputCheck"]["cases"]=[]
                else:revised["stateLayout"]["version"]=1
                with self.assertRaises((ValueError,KeyError,TypeError)):
                    state.validate(revised)
                self.assertNotIn(FIELD,original)

    def test_rejects_change_to_existing_code_when_metadata_is_added(self):
        previous,revised=pair()
        found=[(p,k) for p,page in enumerate(revised["stateLayout"]["current"])
               for k,identity in enumerate(page["blocks"]) if identity==OWNER]
        self.assertEqual(len(found),1)
        p,k=found[0]
        revised["pages"][p]["blocks"][k]["initialCode"] += (
            "\n// Unauthorized change to old curriculum source.\n")
        with self.assertRaises((ValueError,KeyError,TypeError)):
            state.validate_transition(previous,revised)

if __name__=="__main__":
    unittest.main(verbosity=2)
