"""U8L9 authoring contract: additions only, no changed curricular source."""
from copy import deepcopy
from pathlib import Path
import unittest

import stable_state_layout as state
from published_adoption_fixtures import before_adoption, assert_live_owner_addresses

ROOT=Path(__file__).resolve().parents[2]
LESSON=ROOT/("app/src/main/assets/content/es/programming/"
             "cpp_language_memory_oop/composicion_y_herencia_publica/"
             "lesson_reunir_objetos_que_comparten_una_interfaz.json")
OWNER="block_0015_0002"
FIELD="compilerInterfaceCollectionChecks"
SPEC={
    "activity":OWNER,
    "baseClass":"Muestra",
    "simpleClass":"MuestraSimple",
    "identifiedClass":"MuestraIdentificada",
    "vectorAlias":"Muestras",
    "addFunction":"agregar",
    "reportFunction":"informar",
    "writeMethod":"escribir",
    "numberMethod":"numero",
    "destinationParameter":"destino",
    "sourceParameter":"fuente",
    "dataParameter":"datos",
    "streamParameter":"salida",
    "countLabel":"Cantidad:",
    "indexSeparator":":",
    "numberLabel":"Numero:",
    "codeLabel":";Codigo:",
}


def pair():
    original=before_adoption(LESSON)
    edited=deepcopy(original)
    edited[FIELD]=[deepcopy(SPEC)]
    return original,edited


def editor(document):
    locations=[(i,j) for i,page in enumerate(
        document["stateLayout"]["current"])
        for j,identity in enumerate(page["blocks"]) if identity==OWNER]
    assert len(locations)==1
    i,j=locations[0]
    return document["pages"][i]["blocks"][j]


class InterfaceCollectionAuthoring(unittest.TestCase):
    def test_live_owner_validates_independently_of_historical_adoption(self):
        current = assert_live_owner_addresses(self, LESSON, (OWNER,))
        if FIELD in current:
            records = [item for item in current[FIELD]
                       if item["activity"] == OWNER]
            self.assertEqual(records, [SPEC])

    def test_authoring_addition_is_non_destructive(self):
        before,after=pair()
        self.assertNotIn(FIELD,before)
        self.assertEqual(state.validate(before),state.validate(after))
        state.validate_transition(before,after)
        self.assertEqual(before["pages"],after["pages"])
        self.assertEqual(before["stateLayout"],after["stateLayout"])
        old_projection=state.original_projection(before)
        new_projection=state.original_projection(after)
        new_projection.pop(FIELD)
        self.assertEqual(old_projection,new_projection)
        self.assertEqual(editor(before)["initialCode"],editor(after)["initialCode"])
        self.assertEqual(len(editor(after)["outputCheck"]["cases"]),5)

    def test_reject_unknown_owner_symbols_and_labels(self):
        for key,value in {
            "activity":"block_9999_9999",
            "baseClass":"NoExiste",
            "simpleClass":"NoExiste",
            "identifiedClass":"NoExiste",
            "vectorAlias":"NoExiste",
            "addFunction":"anadir",
            "reportFunction":"mostrar",
            "writeMethod":"mostrar",
            "numberMethod":"numero_invalido",
            "destinationParameter":"otra",
            "sourceParameter":"origen",
            "dataParameter":"items",
            "streamParameter":"flujo",
            "countLabel":"bad\nlabel",
            "indexSeparator":"\"",
            "numberLabel":"\\",
            "codeLabel":"bad\nlabel",
        }.items():
            with self.subTest(field=key):
                _,after=pair()
                after[FIELD][0][key]=value
                with self.assertRaises((TypeError,ValueError,KeyError)):
                    state.validate(after)

    def test_reject_invalid_structure_and_collisions(self):
        for scenario in ("duplicate","unknown_field","missing_field",
                         "overlap","wrong_layout","no_cases","wrong_alias"):
            with self.subTest(scenario=scenario):
                _,after=pair()
                spec=after[FIELD][0]
                if scenario=="duplicate":
                    after[FIELD].append(deepcopy(spec))
                elif scenario=="unknown_field":
                    spec["bypass"]=True
                elif scenario=="missing_field":
                    del spec["numberMethod"]
                elif scenario=="overlap":
                    after["compilerAbstractInterfaceChecks"]=[{"activity":OWNER}]
                elif scenario=="wrong_layout":
                    after["stateLayout"]["version"]=1
                elif scenario=="no_cases":
                    editor(after)["outputCheck"]["cases"]=[]
                else:
                    editor(after)["initialCode"]=editor(after)["initialCode"].replace(
                        "using Muestras = std::vector<std::unique_ptr<Muestra>>;",
                        "using Muestras = std::vector<Muestra*>;",1)
                with self.assertRaises((TypeError,ValueError,KeyError)):
                    state.validate(after)

    def test_no_source_or_answer_changes_by_authoring(self):
        for scenario in ("source","oracle"):
            with self.subTest(scenario=scenario):
                before,after=pair()
                block=editor(after)
                if scenario=="source":
                    block["initialCode"]+="\n// Disguised curriculum change\n"
                else:
                    block["outputCheck"]["cases"][0]["outputs"]=["Fabricado"]
                with self.assertRaises((ValueError,TypeError,KeyError)):
                    state.validate_transition(before,after)


if __name__=="__main__":
    unittest.main(verbosity=2)
