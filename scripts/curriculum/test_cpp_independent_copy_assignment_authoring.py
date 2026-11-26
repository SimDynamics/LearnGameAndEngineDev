"""Authorize a new U6L7 private copy objective on an existing stable editor.

The underlying lesson is read-only. No real student state is loaded, and
historical answers, draft keys, owner addresses and source stay unchanged.
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
    "lesson_definir_copia_independiente_y_conservar_el_destino.json"
)
OWNER="block_0014_0002"
FIELD="compilerIndependentCopyAssignmentChecks"
SPEC={
    "activity":OWNER,
    "className":"Celda",
    "memberName":"dato_",
    "parameterName":"otra",
    "hasMethod":"hay",
    "valueMethod":"valor",
    "accessMethod":"acceso",
    "putMethod":"poner",
    "changeMethod":"cambiar",
    "clearMethod":"vaciar",
}


def pair():
    before=before_adoption(LESSON)
    after=deepcopy(before)
    after[FIELD]=[deepcopy(SPEC)]
    return before,after


class IndependentCopyAssignmentAuthoring(unittest.TestCase):
    def test_live_owner_validates_independently_of_historical_adoption(self):
        current = assert_live_owner_addresses(self, LESSON, (OWNER,))
        if FIELD in current:
            records = [item for item in current[FIELD]
                       if item["activity"] == OWNER]
            self.assertEqual(records, [SPEC])

    def test_adds_only_one_verified_compiler_flow_contract(self):
        before,after=pair()
        self.assertNotIn(FIELD,before)
        self.assertEqual(state.validate(before),state.validate(after))
        state.validate_transition(before,after)
        self.assertEqual(before["pages"],after["pages"])
        self.assertEqual(before["stateLayout"],after["stateLayout"])
        original=state.original_projection(after)
        original.pop(FIELD)
        self.assertEqual(state.original_projection(before),original)
        self.assertEqual(after[FIELD],[SPEC])

    def test_invalid_source_owner_or_untrusted_identifiers_are_rejected(self):
        cases=("duplicate","other-owner","unexpected","unknown-block",
               "class","member","parameter","invalid-method",
               "reserved","missing","layout")
        for case in cases:
            with self.subTest(case=case):
                _,after=pair()
                item=after[FIELD][0]
                if case=="duplicate":
                    after[FIELD].append(deepcopy(item))
                elif case=="other-owner":
                    after["compilerDefaultedMovePolicyChecks"]=[{
                        "activity":OWNER,"className":"Celda"
                    }]
                elif case=="unexpected":
                    item["hiddenAnswer"]="forged"
                elif case=="unknown-block":
                    item["activity"]="block_9999_9999"
                elif case=="class":
                    item["className"]="Other"
                elif case=="member":
                    item["memberName"]="unowned"
                elif case=="parameter":
                    item["parameterName"]="source"
                elif case=="invalid-method":
                    item["hasMethod"]="hay();exit(0)"
                elif case=="reserved":
                    item["className"]="__Danger"
                elif case=="missing":
                    del item["accessMethod"]
                else:
                    after["stateLayout"]["version"]=1
                with self.assertRaises((ValueError,KeyError,TypeError)):
                    state.validate(after)

    def test_cannot_rewrite_historical_source_to_get_new_credit(self):
        before,after=pair()
        locations=[(i,j) for i,page in enumerate(after["stateLayout"]["current"])
                   for j,key in enumerate(page["blocks"]) if key==OWNER]
        self.assertEqual(len(locations),1)
        p,i=locations[0]
        after["pages"][p]["blocks"][i]["initialCode"] += (
            "\n// Forged replacement of a published exercise.\n")
        with self.assertRaises((ValueError,KeyError,TypeError)):
            state.validate_transition(before,after)


if __name__=="__main__":
    unittest.main(verbosity=2)
