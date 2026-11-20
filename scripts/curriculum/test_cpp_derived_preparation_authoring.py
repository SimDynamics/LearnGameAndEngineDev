"""Read-only authoring for the original U8L7 exception-safe replacement goal.

The new declaration does not edit the published initialCode, outputCheck
cases, stateLayout, student drafts or previous achievements.
"""
from copy import deepcopy
import json
from pathlib import Path
import unittest

import stable_state_layout as state

ROOT=Path(__file__).resolve().parents[2]
LESSON=ROOT/(
    "app/src/main/assets/content/es/programming/cpp_language_memory_oop/"
    "composicion_y_herencia_publica/"
    "lesson_conservar_recursos_si_falla_la_construccion_derivada.json"
)
OWNER="block_0016_0002"
FIELD="compilerDerivedPreparationChecks"
SPEC={
    "activity":OWNER,
    "baseClass":"Medida",
    "derivedClass":"MedidaIdentificada",
    "balanceClass":"Balance",
    "resourceClass":"Recurso",
    "replacementFunction":"sustituir",
    "destinationParameter":"destino",
    "numberParameter":"numero",
    "codeParameter":"codigo",
    "modeParameter":"modo",
    "balanceParameter":"balance",
    "printMethod":"escribir",
    "resourceAlive":"recursos_vivos",
    "baseCompleted":"bases_completadas",
    "derivedCompleted":"derivadas_completadas",
    "resourceCompleted":"recursos_completados",
    "baseDestroyed":"bases_destruidas",
    "derivedDestroyed":"derivadas_destruidas",
    "resourceDestroyed":"recursos_destruidos",
}


def pair():
    before=json.loads(LESSON.read_text(encoding="utf-8"))
    after=deepcopy(before)
    after[FIELD]=[deepcopy(SPEC)]
    return before,after


class DerivedPreparationAuthoring(unittest.TestCase):
    def test_root_goal_preserves_five_cases_and_original_v2_state(self):
        before,after=pair()
        self.assertNotIn(FIELD,before)
        self.assertEqual(state.validate(before),state.validate(after))
        state.validate_transition(before,after)
        self.assertEqual(before["pages"],after["pages"])
        self.assertEqual(before["stateLayout"],after["stateLayout"])
        prior=state.original_projection(before)
        revised=state.original_projection(after)
        revised.pop(FIELD)
        self.assertEqual(prior,revised)
        positions=[
            (p,b) for p,page in enumerate(after["stateLayout"]["current"])
            for b,name in enumerate(page["blocks"]) if name==OWNER
        ]
        self.assertEqual(len(positions),1)
        p,b=positions[0]
        self.assertEqual(
            len(after["pages"][p]["blocks"][b]["outputCheck"]["cases"]),5)

    def test_reject_unknown_symbol_or_private_owner_collision(self):
        changes={
            "wrong-owner":("activity","block_9999_9999"),
            "wrong-base":("baseClass","NoExiste"),
            "wrong-derived":("derivedClass","NoExiste"),
            "wrong-balance":("balanceClass","NoExiste"),
            "wrong-resource":("resourceClass","NoExiste"),
            "wrong-method":("replacementFunction","otra"),
            "wrong-destination":("destinationParameter","salida"),
            "wrong-number":("numberParameter","valor"),
            "wrong-code":("codeParameter","etiqueta"),
            "wrong-mode":("modeParameter","fallo"),
            "wrong-balance-param":("balanceParameter","registro"),
            "wrong-report":("printMethod","imprimir"),
            "wrong-alive":("resourceAlive","recursos_inventados"),
            "wrong-base-counter":("baseCompleted","cero"),
            "wrong-destroyed":("derivedDestroyed","destruidas"),
            "reserved-identifier":("baseClass","__Medida"),
            "invalid-identifier":("replacementFunction","sustituir();system()"),
        }
        for label,(field,value) in changes.items():
            with self.subTest(case=label):
                original,after=pair()
                after[FIELD][0][field]=value
                with self.assertRaises((ValueError,TypeError,KeyError)):
                    state.validate(after)
                self.assertNotIn(FIELD,original)
        for label in ("duplicate","unknown-property","missing-property",
                      "colliding-owner","wrong-version","missing-first-case"):
            with self.subTest(case=label):
                _,after=pair()
                entry=after[FIELD][0]
                if label=="duplicate":after[FIELD].append(deepcopy(entry))
                elif label=="unknown-property":entry["allowBypass"]=True
                elif label=="missing-property":del entry["modeParameter"]
                elif label=="colliding-owner":
                    after["compilerVirtualLifetimeDispatchChecks"]=[
                        {"activity":OWNER}]
                elif label=="wrong-version":after["stateLayout"]["version"]=1
                else:
                    positions=[
                        (p,b) for p,page in enumerate(after["stateLayout"]["current"])
                        for b,name in enumerate(page["blocks"]) if name==OWNER
                    ]
                    p,b=positions[0]
                    after["pages"][p]["blocks"][b]["outputCheck"]["cases"]=[]
                with self.assertRaises((ValueError,TypeError,KeyError)):
                    state.validate(after)

    def test_rejects_source_or_oracle_change_disguised_as_authorship(self):
        for field in ("initialCode","outputCheck"):
            with self.subTest(field=field):
                before,after=pair()
                positions=[
                    (p,b) for p,page in enumerate(after["stateLayout"]["current"])
                    for b,name in enumerate(page["blocks"]) if name==OWNER
                ]
                p,b=positions[0]
                block=after["pages"][p]["blocks"][b]
                if field=="initialCode":
                    block["initialCode"]+="\n// Unreviewed change\n"
                else:
                    block["outputCheck"]["cases"][0]["outputs"]=["Fabricado\n"]
                with self.assertRaises((ValueError,TypeError,KeyError)):
                    state.validate_transition(before,after)


if __name__=="__main__":
    unittest.main(verbosity=2)
