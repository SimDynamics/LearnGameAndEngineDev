"""Read-only U6L11 root authoring and published-state compatibility checks.

Only optional metadata for an existing Spanish v2 compiler owner is authored.
The published Tramo source, case outputs, progress IDs, and drafts are not
rewritten by these checks.
"""
from copy import deepcopy
import json
from pathlib import Path
import unittest

import stable_state_layout as state

ROOT=Path(__file__).resolve().parents[2]
LESSON=ROOT/("app/src/main/assets/content/es/programming/cpp_language_memory_oop/"
             "estado_y_ciclo_de_vida_de_clases/"
             "lesson_copiar_y_mover_arrays_sin_desajustar_la_cantidad.json")
OWNER="block_0020_0002"
FIELD="compilerArrayCopyMoveChecks"
SPEC={
    "activity":OWNER,
    "className":"Tramo",
    "ownerMember":"datos_",
    "countMember":"cantidad_",
    "copyParameter":"otra",
    "moveParameter":"otra",
    "quantityMethod":"cantidad",
    "accessMethod":"acceso",
    "configureMethod":"configurar",
    "readMethod":"leer",
    "writeMethod":"escribir",
    "clearMethod":"vaciar",
    "sumMethod":"sumar",
}

def pair():
    before=json.loads(LESSON.read_text(encoding="utf-8"))
    after=deepcopy(before)
    after[FIELD]=[deepcopy(SPEC)]
    return before,after


class ArrayCopyMoveAuthoring(unittest.TestCase):
    def test_only_root_addition_without_changing_original_editor(self):
        before,after=pair()
        self.assertNotIn(FIELD,before)
        self.assertEqual(state.validate(before),state.validate(after))
        state.validate_transition(before,after)
        self.assertEqual(before["pages"],after["pages"])
        self.assertEqual(before["stateLayout"],after["stateLayout"])
        past=state.original_projection(before)
        revised=state.original_projection(after)
        revised.pop(FIELD)
        self.assertEqual(past,revised)

    def test_invalid_declaration_and_overlap_fail_closed(self):
        cases=("duplicate","unknown-owner","extra-field","missing-field",
               "wrong-class","wrong-array","wrong-count","wrong-copy-name",
               "wrong-move-name","invalid-method","reserved",
               "overlapping-private-probe","bad-layout","empty-cases")
        for variant in cases:
            with self.subTest(variant=variant):
                old,new=pair()
                spec=new[FIELD][0]
                if variant=="duplicate":new[FIELD].append(deepcopy(spec))
                elif variant=="unknown-owner":spec["activity"]="block_9999_9999"
                elif variant=="extra-field":spec["forcedResult"]=True
                elif variant=="missing-field":del spec["sumMethod"]
                elif variant=="wrong-class":spec["className"]="Falso"
                elif variant=="wrong-array":spec["ownerMember"]="no_existe"
                elif variant=="wrong-count":spec["countMember"]="no_existe"
                elif variant=="wrong-copy-name":spec["copyParameter"]="otro"
                elif variant=="wrong-move-name":spec["moveParameter"]="otro"
                elif variant=="invalid-method":spec["writeMethod"]="escribir();exit(0)"
                elif variant=="reserved":spec["className"]="__Tramo"
                elif variant=="overlapping-private-probe":
                    new["compilerArrayOwnerReadChecks"]=[
                        {"activity":OWNER,"className":"Tramo"}]
                elif variant=="bad-layout":new["stateLayout"]["version"]=1
                else:
                    owners=[(p,b) for p,page in enumerate(
                        new["stateLayout"]["current"])
                        for b,id in enumerate(page["blocks"]) if id==OWNER]
                    self.assertEqual(len(owners),1)
                    p,b=owners[0]
                    new["pages"][p]["blocks"][b]["outputCheck"]["cases"]=[]
                with self.assertRaises((ValueError,KeyError,TypeError)):
                    state.validate(new)
                self.assertNotIn(FIELD,old)

    def test_cannot_change_any_published_code_while_adding_metadata(self):
        old,new=pair()
        owners=[(p,b) for p,page in enumerate(new["stateLayout"]["current"])
                for b,id in enumerate(page["blocks"]) if id==OWNER]
        self.assertEqual(len(owners),1)
        p,b=owners[0]
        new["pages"][p]["blocks"][b]["initialCode"] += (
            "\n// Forged new Tramo program must not inherit old state.\n")
        with self.assertRaises((ValueError,KeyError,TypeError)):
            state.validate_transition(old,new)


if __name__=="__main__":
    unittest.main(verbosity=2)
