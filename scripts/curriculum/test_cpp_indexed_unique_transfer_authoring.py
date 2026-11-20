"""Read-only authoring contract for a private two-owner C++ identity witness.

These tests do not change the published Spanish lesson. Work owns adoption of
the root declaration once the native execution and historical state gate pass.
"""
from copy import deepcopy
import json
from pathlib import Path
import unittest

import stable_state_layout as state

ROOT=Path(__file__).resolve().parents[2]
LESSON=ROOT / ("app/src/main/assets/content/es/programming/"
              "cpp_language_memory_oop/estado_y_ciclo_de_vida_de_clases/"
              "lesson_transferir_propiedad_entre_unique_ptr.json")
OWNER="block_0021_0002"
SPEC={
    "activity": OWNER,
    "signature": "bool Deposito::transferir(int origen, int destino)",
    "className": "Deposito",
    "firstMember": "primero_",
    "secondMember": "segundo_",
    "transferMethod": "transferir",
    "putMethod": "poner",
    "changeMethod": "cambiar",
    "clearMethod": "vaciar",
}


def original():
    return json.loads(LESSON.read_text(encoding="utf-8"))


def revised():
    before=original()
    after=deepcopy(before)
    after["compilerIndexedUniqueTransferChecks"]=[deepcopy(SPEC)]
    return before,after


class IndexedUniqueTransferAuthoring(unittest.TestCase):
    def test_actual_published_owner_is_provable_without_rewriting_it(self):
        before,after=revised()
        original_copy=deepcopy(before)
        self.assertNotIn("compilerIndexedUniqueTransferChecks",before)
        self.assertEqual(state.validate(before),state.validate(after))
        state.validate_transition(before,after)
        self.assertEqual(after["pages"],before["pages"])
        self.assertEqual(after["stateLayout"],before["stateLayout"])
        self.assertEqual(after.get("textSemantics"),before.get("textSemantics"))
        historical_before=state.original_projection(before)
        historical_after=state.original_projection(after)
        # The new root-level declaration remains visible as authoring
        # metadata. All prior historical content and source positions match.
        self.assertIn("compilerIndexedUniqueTransferChecks",historical_after)
        historical_after.pop("compilerIndexedUniqueTransferChecks")
        self.assertEqual(historical_before,historical_after)
        self.assertEqual(before,original_copy)
        self.assertEqual(SPEC,after["compilerIndexedUniqueTransferChecks"][0])

    def test_authored_owner_cannot_be_invented_moved_or_duplicated(self):
        for mode in ("duplicate","missing","wrong-shape","wrong-locale",
                     "empty","renamed-method","missing-class","reused-member",
                     "absolute-owner","wrong-id"):
            with self.subTest(mode=mode):
                before,after=revised()
                entry=after["compilerIndexedUniqueTransferChecks"][0]
                if mode=="duplicate":
                    after["compilerIndexedUniqueTransferChecks"].append(deepcopy(entry))
                elif mode=="missing":
                    entry["activity"]="block_9999_9999"
                elif mode=="wrong-shape":
                    entry["unpublishedAnswer"]="Correcto"
                elif mode=="wrong-locale":
                    after["stateLayout"]["locale"]="fr"
                elif mode=="empty":
                    entry["signature"]=""
                elif mode=="renamed-method":
                    entry["signature"]="bool Deposito::otra(int origen, int destino)"
                elif mode=="missing-class":
                    entry["className"]="NoExiste"
                elif mode=="reused-member":
                    entry["secondMember"]=entry["firstMember"]
                elif mode=="absolute-owner":
                    entry["firstMember"]="__privado"
                else:
                    entry["activity"]=after["stateLayout"]["origin"][0]["blocks"][0]["id"]
                with self.assertRaises((ValueError,KeyError,TypeError)):
                    state.validate(after)
                self.assertNotIn("compilerIndexedUniqueTransferChecks",before)

    def test_other_runtime_prerequisite_and_changed_code_are_rejected(self):
        before,after=revised()
        after["compilerNamedRuleChecks"]=[{
            "activity": OWNER,"path":"","rules": []
        }]
        with self.assertRaises((ValueError,KeyError,TypeError)):
            state.validate(after)
        before,after=revised()
        mapping=after["stateLayout"]["current"]
        at=[(p,i) for p,page in enumerate(mapping)
            for i,identity in enumerate(page["blocks"]) if identity==OWNER]
        self.assertEqual(1,len(at))
        p,i=at[0]
        block=after["pages"][p]["blocks"][i]
        block["initialCode"] += "\n// QA attempt to rewrite protected source\n"
        with self.assertRaises((ValueError,KeyError,TypeError)):
            state.validate_transition(before,after)


if __name__=="__main__":
    unittest.main(verbosity=2)
