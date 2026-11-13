"""General choice wording revision: preserved responses and exact provenance.

The real U9L13 published choice is read but never rewritten by these tests.
"""
from copy import deepcopy
import json
from pathlib import Path
import unittest

import stable_state_layout as layout


SOURCE = (
    Path(__file__).resolve().parents[2]
    / "app/src/main/assets/content/es/programming/cpp_fundamentals"
    / "datos_propios_y_estado_valido"
    / "lesson_construir_y_comprobar_un_modelo_de_pedidos.json"
)
OWNER = "block_0007_0002"
REVISED = (
    "La operación incumple la condición para aceptar la transición, "
    "aunque conserve una cantidad válida."
)


class GeneralChoiceEditorialTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.original = json.loads(SOURCE.read_text(encoding="utf-8"))
        layout.validate(cls.original)
        cls.block = layout._blocks(cls.original)[OWNER]
        assert cls.block["correctIndex"] == 0
        assert cls.block["options"][0] != REVISED

    def revise(self):
        return layout.revise_choice_editorial(self.original, OWNER, 0, REVISED)

    def test_one_compatible_choice_keeps_complete_response_contract(self):
        changed = self.revise()
        old, new = self.block, layout._blocks(changed)[OWNER]
        self.assertEqual(new["options"][0], REVISED)
        self.assertEqual(new["options"][1:], old["options"][1:])
        self.assertEqual(new["correctIndex"], old["correctIndex"])
        self.assertEqual(new["question"], old["question"])
        self.assertEqual(new["explanation"], old["explanation"])
        self.assertEqual(new["reviewFeedback"], old["reviewFeedback"])
        self.assertEqual(changed["stateLayout"]["origin"], self.original["stateLayout"]["origin"])
        self.assertEqual(changed["stateLayout"]["current"], self.original["stateLayout"]["current"])
        self.assertEqual(layout.original_projection(changed), layout.original_projection(self.original))
        layout.validate_transition(self.original, changed)
        self.assertEqual(len(changed["stateLayout"]["choiceRevisions"]), 1)

    def test_rejects_answer_or_provenance_tampering(self):
        valid = self.revise()
        variants = []
        def amended(modify):
            data = deepcopy(valid)
            modify(data, layout._blocks(data)[OWNER])
            variants.append(data)

        amended(lambda doc, block: block["options"].__setitem__(1, "Cambiada sin permiso"))
        amended(lambda doc, block: block.__setitem__("correctIndex", 1))
        amended(lambda doc, block: block.__setitem__("question", "Pregunta sustituida"))
        amended(lambda doc, block: block["reviewFeedback"].__setitem__(0, "Pista sustituida"))
        amended(lambda doc, block: block["options"].__setitem__(0, "Una respuesta distinta"))
        amended(lambda doc, block: doc["stateLayout"]["choiceRevisions"][0].__setitem__("replacement", "Otra versión"))
        amended(lambda doc, block: doc["stateLayout"]["choiceRevisions"][0].__setitem__("optionIndex", 1))
        amended(lambda doc, block: doc["stateLayout"]["choiceRevisions"][0]["block"]["options"].__setitem__(0, "Original falsa"))
        amended(lambda doc, block: doc["stateLayout"]["choiceRevisions"].append(
            deepcopy(doc["stateLayout"]["choiceRevisions"][0])))
        amended(lambda doc, block: doc["stateLayout"].__setitem__("choiceRevisions", []))
        amended(lambda doc, block: doc["stateLayout"]["editorial"].pop())
        for n, candidate in enumerate(variants):
            with self.subTest(variant=n), self.assertRaises(layout.LayoutError):
                layout.validate(candidate)

    def test_rejects_unreviewed_semantic_or_empty_text_change(self):
        for value in ("", "   ", self.block["options"][0]):
            with self.subTest(value=value), self.assertRaises(layout.LayoutError):
                layout.revise_choice_editorial(self.original, OWNER, 0, value)


if __name__ == "__main__":
    unittest.main()
