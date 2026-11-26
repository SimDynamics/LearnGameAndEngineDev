"""Read-only U8L8 authoring, no rewriting of the original abstract interface.

Only an opt-in root declaration is added to a Spanish v2 lesson in memory.
"""
from copy import deepcopy
from pathlib import Path
import unittest

import stable_state_layout as state
from published_adoption_fixtures import before_adoption, assert_live_owner_addresses

ROOT=Path(__file__).resolve().parents[2]
LESSON=ROOT/("app/src/main/assets/content/es/programming/"
             "cpp_language_memory_oop/composicion_y_herencia_publica/"
             "lesson_definir_una_interfaz_abstracta.json")
OWNER="block_0017_0002"
FIELD="compilerAbstractInterfaceChecks"
SPEC={
    "activity":OWNER,
    "baseClass":"Muestra",
    "simpleClass":"MuestraSimple",
    "identifiedClass":"MuestraIdentificada",
    "createFunction":"crear",
    "presentFunction":"presentar",
    "numberMethod":"numero",
    "codeMethod":"codigo",
    "writeMethod":"escribir",
    "streamParameter":"salida",
    "typeParameter":"tipo",
    "numberParameter":"numero",
    "codeParameter":"codigo",
    "objectParameter":"dato",
}


def pair():
    original=before_adoption(LESSON)
    amended=deepcopy(original)
    amended[FIELD]=[deepcopy(SPEC)]
    return original,amended


class AbstractInterfaceAuthoring(unittest.TestCase):
    def test_live_owner_validates_independently_of_historical_adoption(self):
        current = assert_live_owner_addresses(self, LESSON, (OWNER,))
        if FIELD in current:
            records = [item for item in current[FIELD]
                       if item["activity"] == OWNER]
            self.assertEqual(records, [SPEC])

    def test_root_addition_preserves_original_owner_and_five_cases(self):
        before,after=pair()
        self.assertNotIn(FIELD,before)
        self.assertEqual(state.validate(before),state.validate(after))
        state.validate_transition(before,after)
        self.assertEqual(before["pages"],after["pages"])
        self.assertEqual(before["stateLayout"],after["stateLayout"])
        original_projection=state.original_projection(before)
        changed=state.original_projection(after)
        changed.pop(FIELD)
        self.assertEqual(original_projection,changed)
        positions=[
            (p,b) for p,page in enumerate(after["stateLayout"]["current"])
            for b,identity in enumerate(page["blocks"]) if identity==OWNER
        ]
        self.assertEqual(len(positions),1)
        p,b=positions[0]
        editor=after["pages"][p]["blocks"][b]
        self.assertEqual(editor["type"],"cpp_compiler")
        self.assertEqual(len(editor["outputCheck"]["cases"]),5)

    def test_invalid_or_invented_symbols_and_private_owner_collisions(self):
        cases={
            "unknown-owner":("activity","block_9999_9999"),
            "wrong-base":("baseClass","NoExiste"),
            "wrong-simple":("simpleClass","NoExiste"),
            "wrong-identified":("identifiedClass","NoExiste"),
            "wrong-create":("createFunction","crear_otro"),
            "wrong-present":("presentFunction","mostrar"),
            "wrong-number":("numberMethod","dato"),
            "wrong-code":("codeMethod","identificador"),
            "wrong-write":("writeMethod","imprimir"),
            "wrong-stream":("streamParameter","flujo"),
            "wrong-type":("typeParameter","seleccion"),
            "wrong-number-param":("numberParameter","valor"),
            "wrong-code-param":("codeParameter","id"),
            "wrong-object-param":("objectParameter","muestra"),
            "reserved-identifier":("baseClass","__Muestra"),
            "unsafe-identifier":("createFunction","crear();system()"),
        }
        for name,(field,value) in cases.items():
            with self.subTest(case=name):
                published,after=pair()
                after[FIELD][0][field]=value
                with self.assertRaises((ValueError,KeyError,TypeError)):
                    state.validate(after)
                self.assertNotIn(FIELD,published)
        for name in ("duplicate","unknown-field","missing-field",
                     "overlapping-private","wrong-layout","missing-cases"):
            with self.subTest(case=name):
                _,after=pair()
                entry=after[FIELD][0]
                if name=="duplicate":after[FIELD].append(deepcopy(entry))
                elif name=="unknown-field":entry["bypass"]=True
                elif name=="missing-field":del entry["codeMethod"]
                elif name=="overlapping-private":
                    after["compilerDerivedPreparationChecks"]=[{"activity":OWNER}]
                elif name=="wrong-layout":after["stateLayout"]["version"]=1
                else:
                    positions=[
                        (p,b) for p,page in enumerate(after["stateLayout"]["current"])
                        for b,identity in enumerate(page["blocks"]) if identity==OWNER
                    ]
                    p,b=positions[0]
                    after["pages"][p]["blocks"][b]["outputCheck"]["cases"]=[]
                with self.assertRaises((ValueError,KeyError,TypeError)):
                    state.validate(after)

    def test_cannot_alter_published_source_or_answers_in_the_same_transition(self):
        for target in ("initialCode","outputCheck"):
            with self.subTest(target=target):
                before,after=pair()
                positions=[
                    (p,b) for p,page in enumerate(after["stateLayout"]["current"])
                    for b,identity in enumerate(page["blocks"]) if identity==OWNER
                ]
                p,b=positions[0]
                editor=after["pages"][p]["blocks"][b]
                if target=="initialCode":
                    editor["initialCode"]+="\n// Changed protected code\n"
                else:
                    editor["outputCheck"]["cases"][0]["outputs"]=["Fabricado\n"]
                with self.assertRaises((ValueError,KeyError,TypeError)):
                    state.validate_transition(before,after)


if __name__=="__main__":
    unittest.main(verbosity=2)
