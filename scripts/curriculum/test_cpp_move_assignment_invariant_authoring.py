"""Read-only authoring and historical transition checks for P3 U6L8.

The declaration is an optional, root-owned addition. It does not rewrite the
published Muestra editor, the output cases, IDs or user drafts/progress.
"""
from copy import deepcopy
from pathlib import Path
import unittest

import stable_state_layout as state
from published_adoption_fixtures import before_adoption, assert_live_owner_addresses

ROOT=Path(__file__).resolve().parents[2]
LESSON=ROOT / (
    "app/src/main/assets/content/es/programming/cpp_language_memory_oop/"
    "estado_y_ciclo_de_vida_de_clases/"
    "lesson_conservar_invariantes_al_mover_una_clase.json"
)
OWNER="block_0014_0002"
FIELD="compilerMoveAssignmentInvariantChecks"
SPEC={
    "activity":OWNER,
    "className":"Muestra",
    "memberName":"dato_",
    "labelMember":"etiqueta_",
    "parameterName":"otra",
    "hasMethod":"hay",
    "valueMethod":"valor",
    "labelMethod":"etiqueta",
    "accessMethod":"acceso",
    "invariantMethod":"coherente",
    "putMethod":"poner",
    "changeMethod":"cambiar",
    "retagMethod":"reetiquetar",
    "clearMethod":"vaciar",
}

def document_pair():
    previous=before_adoption(LESSON)
    updated=deepcopy(previous)
    updated[FIELD]=[deepcopy(SPEC)]
    return previous,updated


class MoveInvariantAuthoring(unittest.TestCase):
    def test_live_owner_validates_independently_of_historical_adoption(self):
        current = assert_live_owner_addresses(self, LESSON, (OWNER,))
        if FIELD in current:
            records = [item for item in current[FIELD]
                       if item["activity"] == OWNER]
            self.assertEqual(records, [SPEC])

    def test_root_addition_preserves_published_editor_and_history(self):
        previous,updated=document_pair()
        self.assertNotIn(FIELD,previous)
        self.assertEqual(state.validate(previous),state.validate(updated))
        state.validate_transition(previous,updated)
        self.assertEqual(previous["pages"],updated["pages"])
        self.assertEqual(previous["stateLayout"],updated["stateLayout"])
        original=state.original_projection(updated)
        original.pop(FIELD)
        self.assertEqual(state.original_projection(previous),original)

    def test_invalid_identifiers_and_conflicting_owner_rejected(self):
        cases=("duplicate","unknown-owner","unexpected","missing-field",
               "wrong-class","wrong-owner-field","wrong-tag",
               "wrong-parameter","invalid-function","reserved-name",
               "conflicting-other-proof","wrong-layout","no-cases")
        for variant in cases:
            with self.subTest(variant=variant):
                previous,updated=document_pair()
                entry=updated[FIELD][0]
                if variant=="duplicate":updated[FIELD].append(deepcopy(entry))
                elif variant=="unknown-owner":entry["activity"]="block_9999_9999"
                elif variant=="unexpected":entry["hiddenAnswer"]="forged"
                elif variant=="missing-field":entry.pop("invariantMethod")
                elif variant=="wrong-class":entry["className"]="Unrelated"
                elif variant=="wrong-owner-field":entry["memberName"]="not_owned"
                elif variant=="wrong-tag":entry["labelMember"]="not_a_label"
                elif variant=="wrong-parameter":entry["parameterName"]="other"
                elif variant=="invalid-function":entry["putMethod"]="poner();exit(0)"
                elif variant=="reserved-name":entry["className"]="__Muestra"
                elif variant=="conflicting-other-proof":
                    updated["compilerIndependentCopyAssignmentChecks"]=[
                        {"activity":OWNER,"className":"Muestra"}]
                elif variant=="wrong-layout":updated["stateLayout"]["version"]=1
                elif variant=="no-cases":
                    positions=[(p,b) for p,page in
                               enumerate(updated["stateLayout"]["current"])
                               for b,name in enumerate(page["blocks"]) if name==OWNER]
                    self.assertEqual(len(positions),1)
                    p,b=positions[0]
                    updated["pages"][p]["blocks"][b]["outputCheck"]["cases"]=[]
                with self.assertRaises((ValueError,KeyError,TypeError)):
                    state.validate(updated)
                self.assertNotIn(FIELD,previous)

    def test_cannot_rewrite_an_existing_exercise_as_an_invariant(self):
        previous,updated=document_pair()
        locations=[(p,b) for p,page in enumerate(updated["stateLayout"]["current"])
                   for b,name in enumerate(page["blocks"]) if name==OWNER]
        self.assertEqual(len(locations),1)
        p,b=locations[0]
        updated["pages"][p]["blocks"][b]["initialCode"] += (
            "\n// Unauthorized change to original exercise.\n")
        with self.assertRaises((ValueError,KeyError,TypeError)):
            state.validate_transition(previous,updated)


if __name__=="__main__":
    unittest.main(verbosity=2)
