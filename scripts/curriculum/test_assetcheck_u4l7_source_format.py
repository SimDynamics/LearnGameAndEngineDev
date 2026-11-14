"""U4L7 AssetCheck: authentication and lexical whitespace-only source changes."""
from copy import deepcopy
import json
from pathlib import Path
import unittest

import stable_state_layout as state
from code_format_compat import require_source_format

ROOT=Path(__file__).resolve().parents[2]
PATH=ROOT/"app/src/main/assets/content/es/programming/programming_practice_tools/proyecto_assetcheck/lesson_disenar_probar_y_presentar_variante_propia.json"
POSITIONS=((5,2),(6,3),(7,2),(8,2),(9,2),(10,2))

class SourceFormatU4L7(unittest.TestCase):
    def setUp(self):
        self.doc=json.loads(PATH.read_text(encoding="utf-8"))
        self.meta=self.doc["stateLayout"]
        self.archives={entry["id"]:entry["block"] for entry in self.meta["sourceFormatRevisions"]}

    def test_history_and_answers_unchanged(self):
        state.validate(self.doc)
        self.assertEqual(6,len(self.archives))
        identities=[self.meta["current"][p]["blocks"][b] for p,b in POSITIONS]
        self.assertEqual(identities,[proof["id"] for proof in self.meta["editorial"][-6:]])
        historic=deepcopy(self.doc)
        historic["stateLayout"].pop("sourceFormatRevisions")
        historic["stateLayout"]["editorial"]=historic["stateLayout"]["editorial"][:-6]
        for p,b in POSITIONS:
            ident=self.meta["current"][p]["blocks"][b]
            before,after=self.archives[ident],self.doc["pages"][p]["blocks"][b]
            self.assertTrue(require_source_format(before,after),ident)
            slot="initialCode" if after["type"]=="cpp_compiler" else "code"
            self.assertEqual([line for line in before[slot].splitlines() if line.strip()],
                             [line for line in after[slot].splitlines() if line.strip()],ident)
            self.assertGreater(after[slot].count("\n\n"),before[slot].count("\n\n"),ident)
            for key in ("answers","outputCheck","compileCheck","correctOrder"):
                self.assertEqual(before.get(key),after.get(key),ident)
            historic["pages"][p]["blocks"][b]=deepcopy(before)
        state.validate(historic)
        state.validate_transition(historic,self.doc)
        for field in ("origin","current","family","additions"):
            self.assertEqual(historic["stateLayout"].get(field),self.meta.get(field))

    def test_one_first_case_and_all_optional_cases_remain(self):
        editor=self.doc["pages"][6]["blocks"][3]
        cases=editor["outputCheck"]["cases"]
        self.assertEqual(4,len(cases))
        guide=self.doc["pages"][6]["blocks"][4]["text"]
        self.assertIn("primer caso",guide)
        self.assertIn("pruebas son opcionales",guide)
        for c in cases:
            self.assertIn(c["standardInput"].rstrip("\n"),guide)
            self.assertIn(c["outputs"][0].rstrip("\n"),guide)

if __name__=="__main__":
    unittest.main()
