"""Check that the optional U8L10 clone proof never rewrites original lessons.

The published five output cases, page ownership, initial C++ and stable
state addresses remain untouched. Only the new root declaration is tested.
"""
from copy import deepcopy
import json
from pathlib import Path
import unittest

import stable_state_layout as state

ROOT=Path(__file__).resolve().parents[2]
LESSON=ROOT/("app/src/main/assets/content/es/programming/"
             "cpp_language_memory_oop/composicion_y_herencia_publica/"
             "lesson_copiar_objetos_sin_perder_su_clase_concreta.json")
OWNER="block_0015_0002"
FIELD="compilerPolymorphicCloneChecks"
SPEC={
    "activity":OWNER,
    "baseClass":"Muestra",
    "simpleClass":"MuestraSimple",
    "identifiedClass":"MuestraIdentificada",
    "vectorAlias":"Muestras",
    "cloneMethod":"clonar",
    "duplicateFunction":"duplicar",
    "numberMethod":"numero",
    "changeMethod":"cambiar_numero",
    "writeMethod":"escribir",
    "codeMethod":"codigo",
    "sourceParameter":"origen",
    "streamParameter":"salida",
}

def pair():
    original=json.loads(LESSON.read_text(encoding="utf-8"))
    authored=deepcopy(original)
    authored[FIELD]=[deepcopy(SPEC)]
    return original,authored

def editor(document):
    indices=[(p,b) for p,page in enumerate(document["stateLayout"]["current"])
             for b,identity in enumerate(page["blocks"]) if identity==OWNER]
    assert len(indices)==1
    p,b=indices[0]
    return document["pages"][p]["blocks"][b]

class PolymorphicCloneAuthoring(unittest.TestCase):
    def test_opt_in_preserves_original_provenance_and_cases(self):
        before,after=pair()
        self.assertNotIn(FIELD,before)
        self.assertEqual(state.validate(before),state.validate(after))
        state.validate_transition(before,after)
        self.assertEqual(before["pages"],after["pages"])
        self.assertEqual(before["stateLayout"],after["stateLayout"])
        base=state.original_projection(before)
        changed=state.original_projection(after)
        changed.pop(FIELD)
        self.assertEqual(base,changed)
        original=editor(before)
        updated=editor(after)
        self.assertEqual(original,updated)
        self.assertEqual(len(original["outputCheck"]["cases"]),5)

    def test_reject_invalid_identifiers_and_missing_symbols(self):
        for field,value in {
            "activity":"block_9999_9999",
            "baseClass":"Missing",
            "simpleClass":"Missing",
            "identifiedClass":"Missing",
            "vectorAlias":"Missing",
            "cloneMethod":"missing",
            "duplicateFunction":"missing",
            "numberMethod":"missing",
            "changeMethod":"missing",
            "writeMethod":"missing",
            "codeMethod":"missing",
            "sourceParameter":"bad",
            "streamParameter":"bad",
        }.items():
            with self.subTest(field=field):
                _,after=pair()
                after[FIELD][0][field]=value
                with self.assertRaises((ValueError,KeyError,TypeError)):
                    state.validate(after)

    def test_invalid_ownership_and_contract_overlap(self):
        for scenario in ("duplicate","unknown_field","missing_field",
                         "overlap","wrong_layout","no_cases",
                         "remove_clone","bad_vector"):
            with self.subTest(scenario=scenario):
                _,after=pair()
                entry=after[FIELD][0]
                block=editor(after)
                if scenario=="duplicate":
                    after[FIELD].append(deepcopy(entry))
                elif scenario=="unknown_field":
                    entry["unprotected"]=True
                elif scenario=="missing_field":
                    del entry["codeMethod"]
                elif scenario=="overlap":
                    after["compilerInterfaceCollectionChecks"]=[{"activity":OWNER}]
                elif scenario=="wrong_layout":
                    after["stateLayout"]["version"]=1
                elif scenario=="no_cases":
                    block["outputCheck"]["cases"]=[]
                elif scenario=="remove_clone":
                    block["initialCode"]=block["initialCode"].replace(
                        "virtual std::unique_ptr<Muestra> clonar() const = 0;",
                        "virtual std::unique_ptr<Muestra> clonar() const;",1)
                else:
                    block["initialCode"]=block["initialCode"].replace(
                        "using Muestras = std::vector<std::unique_ptr<Muestra>>;",
                        "using Muestras = std::vector<Muestra*>;",1)
                with self.assertRaises((ValueError,KeyError,TypeError)):
                    state.validate(after)

    def test_new_declaration_does_not_authorize_source_or_oracle_edit(self):
        for scenario in ("program","oracle"):
            with self.subTest(scenario=scenario):
                before,after=pair()
                block=editor(after)
                if scenario=="program":
                    block["initialCode"]+="\n// unrequested C++ change\n"
                else:
                    block["outputCheck"]["cases"][0]["outputs"]=["Fabricated"]
                with self.assertRaises((ValueError,KeyError,TypeError)):
                    state.validate_transition(before,after)

if __name__=="__main__":
    unittest.main(verbosity=2)
