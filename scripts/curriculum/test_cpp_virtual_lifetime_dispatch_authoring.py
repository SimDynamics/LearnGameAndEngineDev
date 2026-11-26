"""Protect the published U8L6 lesson when Work opts into virtual-lifetime QA."""
from copy import deepcopy
from pathlib import Path
import unittest

import stable_state_layout as state
from published_adoption_fixtures import before_adoption, assert_live_owner_addresses

ROOT=Path(__file__).resolve().parents[2]
LESSON=ROOT/(
    "app/src/main/assets/content/es/programming/cpp_language_memory_oop/"
    "composicion_y_herencia_publica/"
    "lesson_seguir_llamadas_virtuales_durante_construccion_y_destruccion.json"
)
OWNER="block_0015_0002"
FIELD="compilerVirtualLifetimeDispatchChecks"
SPEC={
    "activity":OWNER,
    "baseClass":"Medida",
    "derivedClass":"MedidaIdentificada",
    "registerMethod":"registrar",
    "writeMethod":"escribir",
    "phaseParameter":"etapa",
    "registerStream":"salida",
    "writeStream":"salida",
}


def pair():
    before=before_adoption(LESSON)
    after=deepcopy(before)
    after[FIELD]=[deepcopy(SPEC)]
    return before,after


class VirtualLifetimeDispatchAuthoring(unittest.TestCase):
    def test_live_owner_validates_independently_of_historical_adoption(self):
        current = assert_live_owner_addresses(self, LESSON, (OWNER,))
        if FIELD in current:
            records = [item for item in current[FIELD]
                       if item["activity"] == OWNER]
            self.assertEqual(records, [SPEC])

    def test_authenticates_root_only_and_keeps_five_cases(self):
        before,after=pair()
        self.assertNotIn(FIELD,before)
        self.assertEqual(state.validate(before),state.validate(after))
        state.validate_transition(before,after)
        self.assertEqual(before["pages"],after["pages"])
        self.assertEqual(before["stateLayout"],after["stateLayout"])
        projection=state.original_projection(after)
        projection.pop(FIELD)
        self.assertEqual(projection,state.original_projection(before))
        positions=[
            (p,b)
            for p,page in enumerate(after["stateLayout"]["current"])
            for b,identity in enumerate(page["blocks"]) if identity==OWNER
        ]
        self.assertEqual(len(positions),1)
        p,b=positions[0]
        self.assertEqual(
            len(after["pages"][p]["blocks"][b]["outputCheck"]["cases"]),5)

    def test_rejects_unsafe_or_unrelated_authoring(self):
        variants={
            "wrong-owner":("activity","block_9999_9999"),
            "wrong-base":("baseClass","Otro"),
            "wrong-derived":("derivedClass","Otro"),
            "wrong-register":("registerMethod","inventado"),
            "wrong-write":("writeMethod","imprimir"),
            "wrong-phase":("phaseParameter","momento"),
            "wrong-register-stream":("registerStream","informe"),
            "wrong-write-stream":("writeStream","destino"),
            "reserved-name":("baseClass","__Medida"),
            "invalid-name":("registerMethod","registrar();std::exit(0)")
        }
        for name,(field,value) in variants.items():
            with self.subTest(name=name):
                before,after=pair()
                after[FIELD][0][field]=value
                with self.assertRaises((ValueError,TypeError,KeyError)):
                    state.validate(after)
                self.assertNotIn(FIELD,before)
        for case in ("duplicate","extra","missing","colliding-private",
                     "changed-state-layout","removed-cases"):
            with self.subTest(case=case):
                _,after=pair()
                item=after[FIELD][0]
                if case=="duplicate":after[FIELD].append(deepcopy(item))
                elif case=="extra":item["skipEvidence"]=True
                elif case=="missing":del item["phaseParameter"]
                elif case=="colliding-private":
                    after["compilerPolymorphicDestructionChecks"]=[{"activity":OWNER}]
                elif case=="changed-state-layout":
                    after["stateLayout"]["version"]=1
                else:
                    positions=[
                        (p,b) for p,page in enumerate(after["stateLayout"]["current"])
                        for b,identity in enumerate(page["blocks"]) if identity==OWNER
                    ]
                    p,b=positions[0]
                    after["pages"][p]["blocks"][b]["outputCheck"]["cases"]=[]
                with self.assertRaises((ValueError,KeyError,TypeError)):
                    state.validate(after)

    def test_prevents_rewriting_original_program_or_output(self):
        for field in ("initialCode","outputCheck"):
            with self.subTest(field=field):
                before,after=pair()
                positions=[
                    (p,b) for p,page in enumerate(after["stateLayout"]["current"])
                    for b,identity in enumerate(page["blocks"]) if identity==OWNER
                ]
                p,b=positions[0]
                editor=after["pages"][p]["blocks"][b]
                if field=="initialCode":
                    editor["initialCode"]+="\n// Changed outside two bodies\n"
                else:
                    editor["outputCheck"]["cases"][0]["outputs"]=["Fabricado\n"]
                with self.assertRaises((ValueError,KeyError,TypeError)):
                    state.validate_transition(before,after)

if __name__=="__main__":
    unittest.main(verbosity=2)
