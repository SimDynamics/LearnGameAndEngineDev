"""Read-only U8L5 authorship of independent polymorphic C++ destruction.

Work may add the root proof without changing the five published cases, source
or any stable state signatures. This suite never writes the lesson JSON.
"""
from copy import deepcopy
from pathlib import Path
import unittest

import stable_state_layout as state
from published_adoption_fixtures import before_adoption, assert_live_owner_addresses

ROOT=Path(__file__).resolve().parents[2]
LESSON=ROOT/(
    "app/src/main/assets/content/es/programming/cpp_language_memory_oop/"
    "composicion_y_herencia_publica/"
    "lesson_destruir_una_derivada_mediante_la_base.json"
)
OWNER="block_0020_0002"
FIELD="compilerPolymorphicDestructionChecks"
SPEC={
    "activity":OWNER,
    "baseClass":"Medida",
    "derivedClass":"MedidaIdentificada",
    "cycleClass":"Ciclo",
    "ownedClass":"Ficha",
    "ownedMember":"ficha_",
    "factoryFunction":"crear_medida",
    "writeMethod":"escribir",
    "numberParameter":"numero",
    "codeParameter":"codigo",
    "cycleParameter":"ciclo",
    "baseAlive":"bases_vivas",
    "derivedAlive":"derivadas_vivas",
    "ownedAlive":"fichas_vivas",
    "baseDestroyed":"bases_destruidas",
    "derivedDestroyed":"derivadas_destruidas",
    "ownedDestroyed":"fichas_destruidas",
}

def pair():
    before=before_adoption(LESSON)
    after=deepcopy(before)
    after[FIELD]=[deepcopy(SPEC)]
    return before,after

class PolymorphicDestructionAuthoring(unittest.TestCase):
    def test_live_owner_validates_independently_of_historical_adoption(self):
        current = assert_live_owner_addresses(self, LESSON, (OWNER,))
        if FIELD in current:
            records = [item for item in current[FIELD]
                       if item["activity"] == OWNER]
            self.assertEqual(records, [SPEC])

    def test_root_only_with_original_v2_state_and_five_cases(self):
        before,after=pair()
        self.assertNotIn(FIELD,before)
        self.assertEqual(state.validate(before),state.validate(after))
        state.validate_transition(before,after)
        self.assertEqual(before["pages"],after["pages"])
        self.assertEqual(before["stateLayout"],after["stateLayout"])
        original=state.original_projection(before)
        revised=state.original_projection(after)
        revised.pop(FIELD)
        self.assertEqual(original,revised)
        locations=[
            (p,b)
            for p,page in enumerate(after["stateLayout"]["current"])
            for b,identity in enumerate(page["blocks"]) if identity==OWNER
        ]
        self.assertEqual(len(locations),1)
        p,b=locations[0]
        self.assertEqual(len(after["pages"][p]["blocks"][b]["outputCheck"]["cases"]),5)

    def test_rejects_untrusted_symbols_and_mismatched_original_source(self):
        bad=(
            "duplicate","missing-owner","extra-field","missing-field",
            "wrong-base","wrong-derived","wrong-cycle","wrong-owned-class",
            "wrong-owned-member","wrong-factory","wrong-write",
            "wrong-write-identifier","wrong-cycle-counter-identifier",
            "wrong-number-parameter","wrong-code-parameter",
            "wrong-cycle-parameter","wrong-base-alive","wrong-base-dead",
            "unsafe-name","reserved-name","different-private-goal",
            "invalid-layout","missing-cases",
        )
        for name in bad:
            with self.subTest(case=name):
                before,after=pair()
                spec=after[FIELD][0]
                changes={
                    "missing-owner":("activity","block_9999_9999"),
                    "wrong-base":("baseClass","Tramo"),
                    "wrong-derived":("derivedClass","Lectura"),
                    "wrong-cycle":("cycleClass","Balance"),
                    "wrong-owned-class":("ownedClass","Otro"),
                    "wrong-owned-member":("ownedMember","nada_"),
                    "wrong-factory":("factoryFunction","fabricar"),
                    "wrong-write":("writeMethod","escribir(); abort()"),
                    "wrong-write-identifier":("writeMethod","imprimir"),
                    "wrong-cycle-counter-identifier":("derivedAlive","vivas"),
                    "wrong-number-parameter":("numberParameter","valor"),
                    "wrong-code-parameter":("codeParameter","etiqueta"),
                    "wrong-cycle-parameter":("cycleParameter","registro"),
                    "wrong-base-alive":("baseAlive","bases_inventadas"),
                    "wrong-base-dead":("baseDestroyed","bases_retiradas"),
                    "unsafe-name":("factoryFunction","crear;instruccion"),
                    "reserved-name":("factoryFunction","__crear"),
                }
                if name in changes:
                    key,value=changes[name]
                    spec[key]=value
                elif name=="duplicate":after[FIELD].append(deepcopy(spec))
                elif name=="extra-field":spec["untrusted"]={"skip":True}
                elif name=="missing-field":del spec["ownedClass"]
                elif name=="different-private-goal":
                    after["compilerArrayReturnTakeChecks"]=[{"activity":OWNER}]
                elif name=="invalid-layout":after["stateLayout"]["version"]=1
                else:
                    locations=[
                        (p,b) for p,page in enumerate(after["stateLayout"]["current"])
                        for b,identity in enumerate(page["blocks"]) if identity==OWNER
                    ]
                    p,b=locations[0]
                    after["pages"][p]["blocks"][b]["outputCheck"]["cases"]=[]
                with self.assertRaises((ValueError,KeyError,TypeError)):
                    state.validate(after)
                self.assertNotIn(FIELD,before)

    def test_rejects_changed_provided_source_or_answers(self):
        for field in ("initialCode","outputCheck"):
            before,after=pair()
            locations=[
                (p,b) for p,page in enumerate(after["stateLayout"]["current"])
                for b,identity in enumerate(page["blocks"]) if identity==OWNER
            ]
            p,b=locations[0]
            block=after["pages"][p]["blocks"][b]
            if field=="initialCode":
                block["initialCode"]+="\n// unreviewed original change\n"
            else:
                block["outputCheck"]["cases"][0]["outputs"]=["Falso\n"]
            with self.subTest(field=field),self.assertRaises(
                    (ValueError,KeyError,TypeError)):
                state.validate_transition(before,after)

if __name__=="__main__":
    unittest.main(verbosity=2)
