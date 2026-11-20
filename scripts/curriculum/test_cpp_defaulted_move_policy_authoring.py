"""Read-only regression for an authored U6L6 move policy root declaration.

Work will publish compatible lesson metadata separately. This test verifies
the new compiler goal can be attached to the original owner without rewriting
the original editor, answer cases, stable state layout or student draft keys.
"""
from copy import deepcopy
import json
from pathlib import Path
import unittest

import stable_state_layout as state

ROOT=Path(__file__).resolve().parents[2]
LESSON=ROOT / ("app/src/main/assets/content/es/programming/"
              "cpp_language_memory_oop/estado_y_ciclo_de_vida_de_clases/"
              "lesson_elegir_copia_y_movimiento_de_una_clase.json")
OWNER="block_0018_0002"
SPEC={
    "activity":OWNER,
    "className":"Ficha",
    "memberName":"dato_",
    "putMethod":"poner",
    "changeMethod":"cambiar",
    "clearMethod":"vaciar",
}
FIELD="compilerDefaultedMovePolicyChecks"


def published():
    return json.loads(LESSON.read_text(encoding="utf-8"))


def updated():
    original=published()
    authored=deepcopy(original)
    authored[FIELD]=[deepcopy(SPEC)]
    return original,authored


class DefaultedMovePolicyAuthoring(unittest.TestCase):
    def test_authentic_u6l6_adds_only_new_root_metadata(self):
        before,after=updated()
        self.assertNotIn(FIELD,before)
        self.assertEqual(state.validate(before),state.validate(after))
        state.validate_transition(before,after)
        self.assertEqual(before["pages"],after["pages"])
        self.assertEqual(before["stateLayout"],after["stateLayout"])
        historical=state.original_projection(after)
        historical.pop(FIELD)
        self.assertEqual(state.original_projection(before),historical)
        self.assertEqual(after[FIELD],[SPEC])

    def test_rejects_fake_owners_and_invalid_policy_names(self):
        cases=("duplicate","missing","unexpected-field","bad-member",
               "different-class","unknown-id","empty","bad-method",
               "reserved-identifier","changed-layout")
        for case in cases:
            with self.subTest(case=case):
                original,after=updated()
                record=after[FIELD][0]
                if case=="duplicate":
                    after[FIELD].append(deepcopy(record))
                elif case=="missing":
                    record["activity"]="block_9999_9999"
                elif case=="unexpected-field":
                    record["hiddenAnswer"]="fake"
                elif case=="bad-member":
                    record["memberName"]="not_a_unique_owner"
                elif case=="different-class":
                    record["className"]="UnrelatedClass"
                elif case=="unknown-id":
                    record["activity"]="random-id"
                elif case=="empty":
                    record["className"]=""
                elif case=="bad-method":
                    record["changeMethod"]="cambiar();std::exit(0)"
                elif case=="reserved-identifier":
                    record["memberName"]="__forged"
                else:
                    after["stateLayout"]["version"]=1
                with self.assertRaises((ValueError, KeyError, TypeError)):
                    state.validate(after)
                self.assertNotIn(FIELD,original)

    def test_existing_source_and_other_root_prerequisites_cannot_be_rewritten(self):
        before,after=updated()
        after["compilerIndexedUniqueTransferChecks"]=[{
            "activity":OWNER,"className":"Ficha"
        }]
        with self.assertRaises((ValueError, KeyError, TypeError)):
            state.validate(after)
        before,after=updated()
        owners=[(p,i) for p,page in enumerate(after["stateLayout"]["current"])
                for i,identity in enumerate(page["blocks"]) if identity==OWNER]
        self.assertEqual(1,len(owners))
        p,i=owners[0]
        after["pages"][p]["blocks"][i]["initialCode"] += (
            "\n// Unauthorized rewrite of the published reference.\n")
        with self.assertRaises((ValueError, KeyError, TypeError)):
            state.validate_transition(before,after)


if __name__=="__main__":
    unittest.main(verbosity=2)
