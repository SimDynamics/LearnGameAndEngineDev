"""Authorial U10L1 dual-interface opt-in must preserve published state/contracts."""
from copy import deepcopy
from pathlib import Path
import unittest

import stable_state_layout as state
from published_adoption_fixtures import before_adoption, assert_live_owner_addresses

ROOT=Path(__file__).resolve().parents[2]
LESSON=ROOT/("app/src/main/assets/content/es/programming/"
             "cpp_language_memory_oop/clases_con_varias_bases/"
             "lesson_reunir_dos_bases_independientes_en_un_objeto.json")
OWNER="block_0007_0011"
FIELD="compilerDualInterfaceChecks"
SPEC={
    "activity":OWNER,
    "valueInterface":"ConValor",
    "reportInterface":"ConInforme",
    "concreteClass":"Muestra",
    "valueMethod":"valor",
    "writeMethod":"escribir",
    "changeMethod":"cambiar",
    "streamParameter":"salida",
    "changeParameter":"nuevo",
    "codeMember":"codigo_",
    "valueMember":"valor_",
    "constructorCodeParameter":"codigo",
    "constructorValueParameter":"valor",
    "reportSeparator":"=",
}

def pair():
    original=before_adoption(LESSON)
    amended=deepcopy(original)
    amended[FIELD]=[deepcopy(SPEC)]
    return original,amended

def owned_block(document):
    indices=[(i,j) for i,p in enumerate(document["stateLayout"]["current"])
             for j,identity in enumerate(p["blocks"]) if identity==OWNER]
    assert len(indices)==1
    i,j=indices[0]
    return document["pages"][i]["blocks"][j]

class DualInterfaceAuthoring(unittest.TestCase):
    def test_live_owner_validates_independently_of_historical_adoption(self):
        current = assert_live_owner_addresses(self, LESSON, (OWNER,))
        if FIELD in current:
            records = [item for item in current[FIELD]
                       if item["activity"] == OWNER]
            self.assertEqual(records, [SPEC])

    def test_authoring_only_addition_preserves_three_inputs_and_all_ids(self):
        before,after=pair()
        self.assertNotIn(FIELD,before)
        self.assertEqual(state.validate(before),state.validate(after))
        state.validate_transition(before,after)
        self.assertEqual(before["pages"],after["pages"])
        self.assertEqual(before["stateLayout"],after["stateLayout"])
        old=state.original_projection(before)
        new=state.original_projection(after)
        new.pop(FIELD)
        self.assertEqual(old,new)
        original=owned_block(before)
        authored=owned_block(after)
        self.assertEqual(original,authored)
        self.assertEqual(len(original["outputCheck"]["cases"]),3)
        self.assertEqual(original["outputCheck"]["cases"][0]["standardInput"],
                         "7 3 -2\n")

    def test_reject_unknown_symbols_or_unsafe_labels(self):
        wrong={
            "activity":"block_9999_9999",
            "valueInterface":"NoExiste",
            "reportInterface":"NoExiste",
            "concreteClass":"NoExiste",
            "valueMethod":"get",
            "writeMethod":"report",
            "changeMethod":"update",
            "streamParameter":"stream",
            "changeParameter":"next",
            "codeMember":"fake_",
            "valueMember":"fake_",
            "constructorCodeParameter":"code",
            "constructorValueParameter":"value",
            "reportSeparator":"\n",
        }
        for key,value in wrong.items():
            with self.subTest(field=key):
                _,after=pair()
                after[FIELD][0][key]=value
                with self.assertRaises((ValueError,KeyError,TypeError)):
                    state.validate(after)

    def test_reject_duplicates_reserved_tokens_overlap_and_wrong_layout(self):
        for case in ("duplicate","extra","missing","overlap","reserved",
                     "bad_label","same_interfaces","wrong_layout",
                     "no_cases","missing_original_method"):
            with self.subTest(case=case):
                _,after=pair()
                entry=after[FIELD][0]
                if case=="duplicate":
                    after[FIELD].append(deepcopy(entry))
                elif case=="extra":
                    entry["bypass"]=True
                elif case=="missing":
                    del entry["changeMethod"]
                elif case=="overlap":
                    after["compilerPolymorphicCloneChecks"]=[{"activity":OWNER}]
                elif case=="reserved":
                    entry["concreteClass"]="__Muestra"
                elif case=="bad_label":
                    entry["reportSeparator"]='";system("danger")'
                elif case=="same_interfaces":
                    entry["reportInterface"]="ConValor"
                elif case=="wrong_layout":
                    after["stateLayout"]["version"]=1
                elif case=="no_cases":
                    owned_block(after)["outputCheck"]["cases"]=[]
                elif case=="missing_original_method":
                    owned_block(after)["initialCode"]=owned_block(after)[
                        "initialCode"].replace("int valor() const override {",
                                               "int valor() const {",1)
                with self.assertRaises((ValueError,TypeError,KeyError)):
                    state.validate(after)

    def test_authoring_does_not_rewrite_cpp_or_answers(self):
        for case in ("code","expected"):
            with self.subTest(case=case):
                before,after=pair()
                editor=owned_block(after)
                if case=="code":
                    editor["initialCode"]+="\n// wrong protected patch\n"
                else:
                    editor["outputCheck"]["cases"][0]["outputs"]=[
                        "Valor: 3\nInforme: 0=0\n"]
                with self.assertRaises((ValueError,TypeError,KeyError)):
                    state.validate_transition(before,after)

if __name__=="__main__":
    unittest.main(verbosity=2)
