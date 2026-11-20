"""Comprueba la revisión general de una consigna matemática publicada en P2.

No modifica el currículo: la propuesta se prueba sobre un commit inmóvil,
con la actividad, sus respuestas y los mapas históricos reales.
"""
from copy import deepcopy
import json
import subprocess
import unittest

import stable_state_layout as layout
import validar_json_programacion as contract


BASELINE = "95bb35f9bbdcaa12855867c0ae078bcda3678f22"
RELATIVE = (
    "app/src/main/assets/content/es/programming/programming_practice_tools/"
    "proyecto_assetcheck/lesson_leer_manifiesto_y_publicar_informe.json"
)
COURSE = "programming_practice_tools"
OWNER = "block_0002_0005"
PAGE, BLOCK = 2, 5
OLD_PROMPT = (
    "Relaciona la secuencia de bytes con la cantidad de líneas físicas. "
    "Las barras inclinadas representan caracteres de escape, no texto literal del manifiesto."
)
NEW_PROMPT = (
    "Relaciona la secuencia de bytes con la cantidad de líneas físicas. "
    "En las etiquetas, las secuencias con barra inversa representan LF o CR; "
    "no son una barra y una letra escritas en el manifiesto."
)


def original_document():
    data = subprocess.check_output(
        ["git", "show", f"{BASELINE}:{RELATIVE}"], cwd=contract.ROOT)
    return json.loads(data)


class PublishedP2MathPromptRevisionTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.original = original_document()
        cls.source = cls.original["pages"][PAGE]["blocks"][BLOCK]
        assert cls.source["type"] == "math_puzzle"
        assert cls.source["prompt"] == OLD_PROMPT
        assert cls.original["stateLayout"]["current"][PAGE]["blocks"][BLOCK] == OWNER
        assert cls.original["stateLayout"]["version"] == 2

    def revised(self):
        return layout.revise_math_prompt_editorial(
            deepcopy(self.original), OWNER, NEW_PROMPT, spans=[])

    def compatible(self, modified, *, page=PAGE, block=BLOCK, course=COURSE,
                   relative=RELATIVE):
        return contract.preserves_previous_exercise(
            relative, page, block, self.source,
            modified["pages"][PAGE]["blocks"][BLOCK],
            previous_document=self.original, current_document=modified,
            course_id=course)

    def test_real_p2_prompt_is_revised_without_changing_its_answers_or_state(self):
        before = deepcopy(self.original)
        after = self.revised()
        current = after["pages"][PAGE]["blocks"][BLOCK]
        self.assertEqual(current["prompt"], NEW_PROMPT)
        self.assertEqual({k: v for k, v in current.items() if k != "prompt"},
                         {k: v for k, v in self.source.items() if k != "prompt"})
        self.assertEqual(self.original, before, "Authoring mutated the published lesson")
        self.assertEqual(layout.validate(before), layout.validate(after))
        layout.validate_transition(before, after)
        self.assertTrue(self.compatible(after))
        self.assertEqual(layout.align_for_previous_validation(before, after)["pages"]
                         [PAGE]["blocks"][BLOCK], self.source)
        for name in ("origin", "family", "current", "additions"):
            self.assertEqual(after["stateLayout"][name],
                             before["stateLayout"][name],
                             f"Unexpected change to historical {name}")
        record = after["stateLayout"]["mathTextRevisions"]
        self.assertEqual(len(record), 1)
        self.assertEqual(record[0]["id"], OWNER)
        self.assertEqual(record[0]["field"], "/prompt")
        self.assertEqual(json.loads(record[0]["rawJson"]), self.source)
        markers = [f for f in after["textSemantics"]["fields"]
                   if f["block"] == OWNER and f["path"] == "/prompt"]
        self.assertEqual(len(markers), 1)
        self.assertEqual(markers[0]["spans"], [])

    def test_wrong_course_location_and_unrelated_owner_are_not_authorized(self):
        revised = self.revised()
        for kwargs in ({"course": "cpp_fundamentals"},
                       {"page": PAGE - 1}, {"block": BLOCK - 1},
                       {"relative": RELATIVE.replace("assetcheck", "otro_proyecto")}):
            with self.subTest(kwargs=kwargs):
                self.assertFalse(self.compatible(revised, **kwargs))

    def test_response_or_history_tampering_cannot_borrow_the_prompt_revision(self):
        revised = self.revised()
        for mutation in ("response", "archive", "proof", "family", "marker"):
            with self.subTest(mutation=mutation):
                changed = deepcopy(revised)
                if mutation == "response":
                    changed["pages"][PAGE]["blocks"][BLOCK]["responses"][0]["sources"][0]["label"] += "!"
                elif mutation == "archive":
                    changed["stateLayout"]["mathTextRevisions"][0]["rawJson"] = "{}"
                elif mutation == "proof":
                    changed["stateLayout"]["editorial"][-1]["afterSha256"] = "0" * 64
                elif mutation == "family":
                    changed["stateLayout"]["family"]["es"]["sha256"] = "0" * 64
                else:
                    changed["textSemantics"]["fields"] = [
                        field for field in changed["textSemantics"]["fields"]
                        if (field["block"], field["path"]) != (OWNER, "/prompt")]
                # A document can be internally well-formed yet substitute
                # another published language family. Authenticity belongs to
                # the comparison with the exact preceding publication.
                with self.assertRaises((ValueError, KeyError, TypeError)):
                    layout.validate_transition(self.original, changed)
                self.assertFalse(self.compatible(changed))


if __name__ == "__main__":
    unittest.main()
